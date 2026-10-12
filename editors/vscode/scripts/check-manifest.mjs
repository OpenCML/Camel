#!/usr/bin/env node
// Checks that the files of the extension agree with each other, without loading any package:
// package.json, language-configuration.json and syntaxes/camel.tmLanguage.json are valid JSON;
// the paths the manifest contributes exist; the grammar's scope name and the language id are the
// ones the manifest registers; the commands and settings the extension implements are
// contributed; the two Camel-specific semantic token modifiers ('mutable' for `var`, 'sync' for
// `sync func`) are declared, mapped to a TextMate scope and styled by default; every repository
// rule the grammar includes exists.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const EXT_ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const errors = []

function readJson(relative) {
    const file = path.join(EXT_ROOT, relative)
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch (err) {
        errors.push(`${relative}: ${err.message}`)
        return null
    }
}

function check(condition, message) {
    if (!condition) errors.push(message)
}

const manifest = readJson('package.json')
const grammar = readJson('syntaxes/camel.tmLanguage.json')
const languageConfig = readJson('language-configuration.json')

if (manifest && grammar && languageConfig) {
    const contributes = manifest.contributes || {}

    const language = (contributes.languages || []).find((entry) => entry.id === 'camel')
    check(language, "package.json: no language with id 'camel'")
    if (language) {
        check((language.extensions || []).includes('.cml'), "package.json: language 'camel' does not register the extension .cml")
        check(
            language.configuration && fs.existsSync(path.join(EXT_ROOT, language.configuration)),
            `package.json: language configuration ${language.configuration} does not exist`
        )
    }

    const grammarEntry = (contributes.grammars || []).find((entry) => entry.language === 'camel')
    check(grammarEntry, "package.json: no grammar for the language 'camel'")
    if (grammarEntry) {
        check(fs.existsSync(path.join(EXT_ROOT, grammarEntry.path)), `package.json: grammar ${grammarEntry.path} does not exist`)
        check(
            grammarEntry.scopeName === grammar.scopeName,
            `package.json registers the scope ${grammarEntry.scopeName}, the grammar is ${grammar.scopeName}`
        )
    }
    check(grammar.scopeName === 'source.camel', `the grammar's scope name is ${grammar.scopeName}, expected source.camel`)

    // Every '#name' include of the grammar names a repository rule.
    const repository = grammar.repository || {}
    const visitIncludes = (node, where) => {
        if (Array.isArray(node)) {
            node.forEach((item, index) => visitIncludes(item, `${where}[${index}]`))
        } else if (node && typeof node === 'object') {
            if (typeof node.include === 'string' && node.include.startsWith('#')) {
                check(repository[node.include.slice(1)], `grammar: ${where} includes ${node.include}, which is not in the repository`)
            }
            for (const [key, value] of Object.entries(node)) visitIncludes(value, `${where}.${key}`)
        }
    }
    visitIncludes(grammar.patterns, 'patterns')
    visitIncludes(repository, 'repository')

    // Every scope name the grammar assigns ends in '.camel'.
    const visitNames = (node) => {
        if (Array.isArray(node)) {
            node.forEach(visitNames)
        } else if (node && typeof node === 'object') {
            for (const key of ['name', 'contentName']) {
                if (typeof node[key] === 'string') {
                    for (const scope of node[key].split(/\s+/)) {
                        check(scope.endsWith('.camel'), `grammar: scope '${scope}' does not end in .camel`)
                    }
                }
            }
            Object.values(node).forEach(visitNames)
        }
    }
    visitNames(repository)

    // The commands and settings src/extension.ts implements.
    const commands = (contributes.commands || []).map((entry) => entry.command)
    for (const command of ['camel.restartServer', 'camel.showGraph', 'camel.runWithPasses']) {
        check(commands.includes(command), `package.json: command ${command} is not contributed`)
    }
    check((manifest.activationEvents || []).includes('onLanguage:camel'), 'package.json: no activation event onLanguage:camel')

    const properties = (contributes.configuration || {}).properties || {}
    for (const key of ['camel.server.path', 'camel.path', 'camel.graph.passes', 'camel.run.passes']) {
        check(properties[key], `package.json: setting ${key} is not contributed`)
    }

    // The custom semantic token modifiers.
    const modifiers = (contributes.semanticTokenModifiers || []).map((entry) => entry.id)
    const scopeMaps = (contributes.semanticTokenScopes || []).filter((entry) => !entry.language || entry.language === 'camel')
    const mappedSelectors = scopeMaps.flatMap((entry) => Object.keys(entry.scopes || {}))
    const rules = ((contributes.configurationDefaults || {})['editor.semanticTokenColorCustomizations'] || {}).rules || {}
    const usesModifier = (selector, modifier) => selector.split(':')[0].split('.').slice(1).includes(modifier)
    for (const modifier of ['mutable', 'sync']) {
        check(modifiers.includes(modifier), `package.json: semantic token modifier '${modifier}' is not declared`)
        check(
            mappedSelectors.some((selector) => usesModifier(selector, modifier)),
            `package.json: no semanticTokenScopes entry for the modifier '${modifier}'`
        )
        check(
            Object.keys(rules).some((selector) => usesModifier(selector, modifier)),
            `package.json: no default editor.semanticTokenColorCustomizations rule for the modifier '${modifier}'`
        )
    }

    // The extension's entry point is what tsc produces from src/extension.ts.
    check(manifest.main === './out/extension.js', `package.json: main is ${manifest.main}, expected ./out/extension.js`)
    check(fs.existsSync(path.join(EXT_ROOT, 'src', 'extension.ts')), 'src/extension.ts does not exist')

    // The regular expressions of the language configuration compile.
    const regexes = [
        ['wordPattern', languageConfig.wordPattern],
        ['indentationRules.increaseIndentPattern', (languageConfig.indentationRules || {}).increaseIndentPattern],
        ['indentationRules.decreaseIndentPattern', (languageConfig.indentationRules || {}).decreaseIndentPattern],
        ...(languageConfig.onEnterRules || []).flatMap((rule, index) => [
            [`onEnterRules[${index}].beforeText`, rule.beforeText],
            [`onEnterRules[${index}].afterText`, rule.afterText],
        ]),
    ]
    for (const [where, source] of regexes) {
        if (source === undefined) continue
        try {
            new RegExp(typeof source === 'string' ? source : source.pattern)
        } catch (err) {
            errors.push(`language-configuration.json: ${where}: ${err.message}`)
        }
    }
    check(languageConfig.comments && languageConfig.comments.lineComment === '//', 'language-configuration.json: the line comment is not //')
}

if (errors.length > 0) {
    for (const error of errors) console.log(`error: ${error}`)
    console.log(`manifest check failed: ${errors.length} error(s)`)
    process.exit(1)
}
console.log('manifest check passed')
