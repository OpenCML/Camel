import fs from 'fs'
import path from 'path'
import { execSync } from 'child_process'
import sqlite3 from 'sqlite3'
import { copySync, ensureDirSync } from 'fs-extra/esm'
import { BASEDIR, logStep, logFail, logDone, logWarn } from './common.js'

function getDependencies() {
    logStep('Reading conanfile.txt...')
    try {
        const content = fs.readFileSync('conanfile.txt', 'utf-8')
        const lines = content.split('\n') // 按行分割文本
        let capturing = false
        const requires = []

        lines.forEach((line) => {
            if (line.trim() === '[requires]') {
                capturing = true // 开始捕获
            } else if (line.trim().startsWith('[') && capturing) {
                capturing = false // 遇到新的段落，停止捕获
            } else if (capturing) {
                // 处理可能存在的注释和空行
                const [recipe] = line.trim().split('#')[0].trim().split('@')[0].split(' ')
                if (recipe) {
                    requires.push(recipe)
                }
            }
        })

        logDone('Detected dependencies: ' + requires.join(', '))
        return requires
    } catch (error) {
        logFail('Failed to read conanfile.txt')
        throw error
    }
}

async function buildDependencyMap(storagePath, dependencies) {
    logStep('Querying Conan database...')

    return new Promise((resolve, reject) => {
        const db = new sqlite3.Database(path.join(storagePath, 'cache.sqlite3'))
        const depMap = new Map()

        db.all(`SELECT reference, path FROM packages`, (err, rows) => {
            if (err) {
                db.close()
                reject(err)
                return
            }

            rows.forEach((row) => {
                if (dependencies.some((dep) => row.reference.includes(dep))) {
                    const [pkgName] = row.reference.split('/')
                    const fullPath = path.join(storagePath, row.path)
                    depMap.set(pkgName, [...(depMap.get(pkgName) || []), fullPath])
                }
            })

            db.close()

            logDone('Dependency map built successfully: ')
            depMap.forEach((value, key) => {
                console.log(`  ${key}: ${value.join(', ')}`)
            })
            resolve(depMap)
        })
    })
}

// 获取Conan存储路径
function getConanStoragePath(dependencies) {
    logStep('Determining Conan storage path...')

    // 获取两个不同依赖的路径
    const [dep1, dep2] = dependencies.slice(0, 2)
    if (!dep1 || !dep2) throw new Error('Need at least two dependencies')

    const getPath = (dep) => execSync(`conan cache path ${dep}`, { encoding: 'utf-8' }).trim()

    try {
        const path1 = getPath(dep1)
        const path2 = getPath(dep2)

        // 查找最长公共前缀
        let commonPath = ''
        const minLen = Math.min(path1.length, path2.length)
        for (let i = 0; i < minLen; i++) {
            if (path1[i] === path2[i]) {
                commonPath += path1[i]
            } else {
                break
            }
        }

        // 验证路径
        const storagePath = commonPath
        if (!fs.existsSync(path.join(storagePath, 'cache.sqlite3'))) {
            throw new Error(`Invalid storage path ${storagePath}`)
        }

        logDone(`Found Conan storage at: ${storagePath}`)
        return storagePath
    } catch (e) {
        logFail('Failed to determine storage path')
        throw e
    }
}

// 复制到vendor目录
function copyToVendor(depMap) {
    logStep("Copying dependencies' src to vendor directory...")

    ensureDirSync('./vendor')
    depMap.forEach((paths, pkgName) => {
        const src = path.join(paths[0], 'p', 'include')
        const dest = path.join('./vendor', pkgName)

        try {
            copySync(src, dest)
            logDone(`Copied ${pkgName} to vendor directory`)
        } catch (e) {
            logWarn(`Failed to copy ${pkgName}: ${e.message}`)
        }
    })
}

// 更新C/C++配置
function updateCppProperties(depMap) {
    logStep('Updating c_cpp_properties.json...')
    const configDir = path.join(BASEDIR, '.vscode')
    ensureDirSync(configDir)

    const configPath = path.join(configDir, 'c_cpp_properties.json')
    let config = { configurations: [{}] }

    try {
        if (fs.existsSync(configPath)) {
            config = JSON.parse(fs.readFileSync(configPath, 'utf-8'))
        }

        config.configurations.forEach((cfg) => {
            cfg.includePath = cfg.includePath || []
            cfg.cppStandard = 'c++20'
            const newPaths = []

            newPaths.push('${workspaceFolder}')
            newPaths.push(path.join('${workspaceFolder}', 'src'))
            newPaths.push(path.join('${workspaceFolder}', 'vendor'))
            newPaths.push(path.join('${workspaceFolder}', 'third_party'))

            depMap.forEach((_, pkgName) => {
                newPaths.push(path.join('${workspaceFolder}', 'vendor', pkgName))
            })

            newPaths
                .map((p) => p.replace(/\\/g, '/'))
                .forEach((p) => {
                    if (!cfg.includePath.includes(p)) {
                        cfg.includePath.push(p)
                    }
                })
        })

        fs.writeFileSync(configPath, JSON.stringify(config, null, 2))
        logDone('Updated include paths')
    } catch (e) {
        logWarn(`Failed to update c_cpp_properties.json: ${e.message}`)
    }
}

async function main() {
    const arg = process.argv[2]

    try {
        // 1. 获取依赖项
        const dependencies = getDependencies()
        // 2. 获取存储路径
        const storagePath = getConanStoragePath(dependencies)
        // 3. 构建依赖映射
        const depMap = await buildDependencyMap(storagePath, dependencies)

        if (arg === 'vsc') {
            // 4. 复制到vendor目录
            copyToVendor(depMap)
            // 5. 更新C++配置
            updateCppProperties(depMap)
        } else {
            logWarn('No operation specified. Use "vsc" to update VSCode settings.')
        }

        logDone('All operations completed successfully!')
    } catch (e) {
        logFail(`Fatal error: ${e.message}`)
        process.exit(1)
    }
}

main()
