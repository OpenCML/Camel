// Camel for VS Code: a thin client of the camel-ls language server.
//
// The extension itself only contributes the grammar and the language configuration (see
// package.json), starts `camel-ls --stdio`, and adds two commands that have no counterpart in
// the language server protocol: showing the graph of a function (custom request `camel/graph`)
// and running the current file through a pass sequence.

import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions, State } from 'vscode-languageclient/node';

const LANGUAGE_ID = 'camel';
const GRAPH_SCHEME = 'camel-graph';
const LAST_PASSES_KEY = 'camel.run.lastPasses';

let client: LanguageClient | undefined;
let output: vscode.OutputChannel;
let graphProvider: GraphContentProvider;

// ---------------------------------------------------------------------------------------------
// Locating the executables
// ---------------------------------------------------------------------------------------------

function isFile(candidate: string): boolean {
    try {
        return fs.statSync(candidate).isFile();
    } catch {
        return false;
    }
}

/** The names an executable may have on this platform: `camel-ls`, and `camel-ls.exe` on Windows. */
function executableNames(name: string): string[] {
    if (process.platform !== 'win32') {
        return [name];
    }
    const extensions = (process.env.PATHEXT || '.EXE;.CMD;.BAT').split(';').filter(Boolean);
    return [...extensions.map((ext) => name + ext.toLowerCase()), name];
}

function findIn(dir: string, name: string): string | undefined {
    for (const candidate of executableNames(name)) {
        const full = path.join(dir, candidate);
        if (isFile(full)) {
            return full;
        }
    }
    return undefined;
}

interface Resolved {
    /** The executable, when one was found. */
    path?: string;
    /** Where it was looked for, for the error message. */
    tried: string[];
}

/**
 * Finds `name`: the setting `settingKey` when it is set (nothing else is tried then), else
 * `$CAMEL_HOME/bin`, else the directories of `PATH`.
 */
function resolveExecutable(name: string, settingKey: string): Resolved {
    const tried: string[] = [];
    const configured = (vscode.workspace.getConfiguration().get<string>(settingKey) || '').trim();
    if (configured) {
        const expanded = configured.replace(/^~(?=$|[\\/])/, process.env.HOME || process.env.USERPROFILE || '~');
        tried.push(`${settingKey}: ${expanded}`);
        if (isFile(expanded)) {
            return { path: expanded, tried };
        }
        // A directory, or a path without the platform's extension.
        const found = findIn(path.dirname(expanded), path.basename(expanded)) || findIn(expanded, name);
        return { path: found, tried };
    }

    const home = process.env.CAMEL_HOME;
    if (home) {
        const bin = path.join(home, 'bin');
        tried.push(`$CAMEL_HOME/bin (${bin})`);
        const found = findIn(bin, name);
        if (found) {
            return { path: found, tried };
        }
    } else {
        tried.push('$CAMEL_HOME (not set)');
    }

    tried.push('PATH');
    for (const dir of (process.env.PATH || '').split(path.delimiter)) {
        if (!dir) {
            continue;
        }
        const found = findIn(dir, name);
        if (found) {
            return { path: found, tried };
        }
    }
    return { tried };
}

async function reportMissing(name: string, settingKey: string, tried: string[]): Promise<void> {
    const message =
        `Camel: cannot find the '${name}' executable (looked in ${tried.join(', ')}). ` +
        `Build Camel and set CAMEL_HOME, or set "${settingKey}" to the full path of ${name}.`;
    output.appendLine(message);
    const open = 'Open Settings';
    const choice = await vscode.window.showErrorMessage(message, open);
    if (choice === open) {
        await vscode.commands.executeCommand('workbench.action.openSettings', settingKey);
    }
}

// ---------------------------------------------------------------------------------------------
// The language client
// ---------------------------------------------------------------------------------------------

async function startClient(): Promise<void> {
    const server = resolveExecutable('camel-ls', 'camel.server.path');
    if (!server.path) {
        await reportMissing('camel-ls', 'camel.server.path', server.tried);
        return;
    }
    output.appendLine(`Starting ${server.path} --stdio`);

    // No `transport`: the client then talks over the process's standard streams and passes the
    // arguments as given.
    const serverOptions: ServerOptions = {
        command: server.path,
        args: ['--stdio'],
    };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: LANGUAGE_ID },
            { scheme: 'untitled', language: LANGUAGE_ID },
        ],
        outputChannel: output,
    };

    const started = new LanguageClient(LANGUAGE_ID, 'Camel Language Server', serverOptions, clientOptions);
    client = started;
    try {
        await started.start();
    } catch (err) {
        if (client === started) {
            client = undefined;
        }
        void vscode.window.showErrorMessage(
            `Camel: the language server failed to start (${server.path}): ${errorText(err)}. ` +
                'See the output channel "Camel Language Server".'
        );
    }
}

async function stopClient(): Promise<void> {
    const running = client;
    client = undefined;
    if (!running) {
        return;
    }
    try {
        await running.stop();
    } catch (err) {
        output.appendLine(`Stopping the language server failed: ${errorText(err)}`);
    }
}

async function restartClient(): Promise<void> {
    await stopClient();
    await startClient();
    graphProvider.refreshAll();
}

function errorText(err: unknown): string {
    return err instanceof Error ? err.message : String(err);
}

// ---------------------------------------------------------------------------------------------
// Camel: Show Graph of Current Function
// ---------------------------------------------------------------------------------------------

interface GraphParams {
    textDocument: { uri: string };
    position: { line: number; character: number };
    passes?: string[];
}

interface GraphResult {
    function: string | null;
    passes: string[];
    text: string;
    diagnostics?: string;
}

/** What a graph document shows: the source document and the position the command was run at. */
interface GraphTarget {
    source: string;
    line: number;
    character: number;
}

function graphUri(target: GraphTarget): vscode.Uri {
    const base = path.posix.basename(vscode.Uri.parse(target.source).path) || 'graph';
    return vscode.Uri.from({
        scheme: GRAPH_SCHEME,
        // The `.dot` suffix lets a Graphviz extension recognise the document.
        path: `/${base}.L${target.line + 1}.dot`,
        query: JSON.stringify(target),
    });
}

function graphTarget(uri: vscode.Uri): GraphTarget | undefined {
    try {
        const target = JSON.parse(uri.query) as GraphTarget;
        return typeof target.source === 'string' ? target : undefined;
    } catch {
        return undefined;
    }
}

/** DOT comments, so that a message is still a valid (empty) DOT document. */
function asDotComment(text: string): string {
    return text
        .split(/\r?\n/)
        .map((line) => `// ${line}`.trimEnd())
        .join('\n');
}

/**
 * Read-only documents `camel-graph:/<file>.L<line>.dot?<target>` whose content is the DOT text
 * the server returns for `camel/graph`.
 */
class GraphContentProvider implements vscode.TextDocumentContentProvider {
    private readonly changed = new vscode.EventEmitter<vscode.Uri>();
    readonly onDidChange = this.changed.event;

    async provideTextDocumentContent(uri: vscode.Uri): Promise<string> {
        const target = graphTarget(uri);
        if (!target) {
            return asDotComment('Camel: not a graph document.');
        }
        if (!client || client.state !== State.Running) {
            return asDotComment('Camel: the language server is not running (see "Camel: Restart Language Server").');
        }
        const passes = vscode.workspace
            .getConfiguration('camel', vscode.Uri.parse(target.source))
            .get<string[]>('graph.passes', []);
        const params: GraphParams = {
            textDocument: { uri: target.source },
            position: { line: target.line, character: target.character },
        };
        if (passes.length > 0) {
            params.passes = passes;
        }

        let result: GraphResult | null;
        try {
            result = await client.sendRequest<GraphResult | null>('camel/graph', params);
        } catch (err) {
            return asDotComment(`Camel: the request camel/graph failed: ${errorText(err)}`);
        }
        if (!result) {
            return asDotComment('Camel: the server returned no graph.');
        }

        const header = [
            result.function ? `function: ${result.function}` : 'no function at the cursor',
            `passes: ${result.passes && result.passes.length > 0 ? result.passes.join(' ') : '(none)'}`,
        ];
        const parts = [asDotComment(header.join('\n'))];
        if (result.diagnostics && result.diagnostics.trim() !== '') {
            parts.push(asDotComment(result.diagnostics.trimEnd()));
        }
        if (result.text && result.text.trim() !== '') {
            parts.push(result.text.trimEnd());
        }
        return parts.join('\n') + '\n';
    }

    refresh(uri: vscode.Uri): void {
        this.changed.fire(uri);
    }

    /** Re-requests every open graph of `source`. */
    refreshFor(source: string): void {
        for (const doc of vscode.workspace.textDocuments) {
            if (doc.uri.scheme === GRAPH_SCHEME && graphTarget(doc.uri)?.source === source) {
                this.changed.fire(doc.uri);
            }
        }
    }

    refreshAll(): void {
        for (const doc of vscode.workspace.textDocuments) {
            if (doc.uri.scheme === GRAPH_SCHEME) {
                this.changed.fire(doc.uri);
            }
        }
    }

    dispose(): void {
        this.changed.dispose();
    }
}

async function showGraph(): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== LANGUAGE_ID) {
        void vscode.window.showInformationMessage('Camel: open a .cml file and put the cursor in a function first.');
        return;
    }
    if (!client || client.state !== State.Running) {
        void vscode.window.showErrorMessage(
            'Camel: the language server is not running. Check the setting "camel.server.path", then run "Camel: Restart Language Server".'
        );
        return;
    }
    const position = editor.selection.active;
    const uri = graphUri({
        source: editor.document.uri.toString(),
        line: position.line,
        character: position.character,
    });
    // The same position may have been shown before: ask again instead of showing the old text.
    const alreadyOpen = vscode.workspace.textDocuments.some((open) => open.uri.toString() === uri.toString());
    const doc = await vscode.workspace.openTextDocument(uri);
    if (alreadyOpen) {
        graphProvider.refresh(uri);
    }
    await vscode.window.showTextDocument(doc, {
        viewColumn: vscode.ViewColumn.Beside,
        preserveFocus: true,
        preview: false,
    });
}

// ---------------------------------------------------------------------------------------------
// Camel: Run Current File with Passes...
// ---------------------------------------------------------------------------------------------

async function runWithPasses(context: vscode.ExtensionContext): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== LANGUAGE_ID) {
        void vscode.window.showInformationMessage('Camel: open a .cml file first.');
        return;
    }
    const document = editor.document;
    if (document.uri.scheme !== 'file') {
        void vscode.window.showInformationMessage('Camel: save the file first; camel runs files on disk.');
        return;
    }

    const configured = vscode.workspace.getConfiguration('camel', document.uri).get<string[]>('run.passes', []);
    const remembered = context.workspaceState.get<string>(LAST_PASSES_KEY);
    const input = await vscode.window.showInputBox({
        title: 'Camel: Run Current File with Passes',
        prompt: 'Passes, applied in order and separated by spaces. Empty: the default (std::nvm).',
        placeHolder: 'std::inline std::fvm',
        value: remembered !== undefined ? remembered : configured.join(' '),
    });
    if (input === undefined) {
        return;
    }
    const passes = input.split(/[\s,]+/).filter(Boolean);
    await context.workspaceState.update(LAST_PASSES_KEY, passes.join(' '));

    const camel = resolveExecutable('camel', 'camel.path');
    if (!camel.path) {
        await reportMissing('camel', 'camel.path', camel.tried);
        return;
    }
    if (document.isDirty && !(await document.save())) {
        return;
    }

    // A task runs in the integrated terminal and gets the arguments as a vector, so that no
    // quoting depends on the user's shell.
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    const task = new vscode.Task(
        { type: 'camel', file: document.uri.fsPath, passes },
        folder ?? vscode.TaskScope.Workspace,
        `${path.basename(document.uri.fsPath)} ${passes.join(' ')}`.trim(),
        'camel',
        new vscode.ProcessExecution(camel.path, [document.uri.fsPath, ...passes], {
            cwd: folder ? folder.uri.fsPath : path.dirname(document.uri.fsPath),
        })
    );
    task.presentationOptions = {
        reveal: vscode.TaskRevealKind.Always,
        panel: vscode.TaskPanelKind.Dedicated,
        clear: true,
        focus: false,
    };
    await vscode.tasks.executeTask(task);
}

// ---------------------------------------------------------------------------------------------

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    output = vscode.window.createOutputChannel('Camel Language Server');
    graphProvider = new GraphContentProvider();

    context.subscriptions.push(
        output,
        graphProvider,
        vscode.workspace.registerTextDocumentContentProvider(GRAPH_SCHEME, graphProvider),
        vscode.commands.registerCommand('camel.restartServer', restartClient),
        vscode.commands.registerCommand('camel.showGraph', showGraph),
        vscode.commands.registerCommand('camel.runWithPasses', () => runWithPasses(context)),
        vscode.workspace.onDidSaveTextDocument((document) => {
            if (document.languageId === LANGUAGE_ID) {
                graphProvider.refreshFor(document.uri.toString());
            }
        }),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration('camel.server.path')) {
                void restartClient();
            } else if (event.affectsConfiguration('camel.graph.passes')) {
                graphProvider.refreshAll();
            }
        })
    );

    await startClient();
}

export async function deactivate(): Promise<void> {
    await stopClient();
}
