# Local editor runtime synchronization

The post-commit hook can copy the latest completed build to an independent editor runtime.
It is opt-in per local checkout; it does not build Camel or publish anything.

Enable it after preparing the fixed runtime directory:

```powershell
git config --local camel.runtimeSyncTarget "D:/Projects/OpenCML-Ext/.local/camel/runtime"
```

Every commit makes one attempt. After the new copy is ready, the hook terminates Camel
processes whose executable path is inside the fixed destination, then installs it once.
It does not close VS Code or stop Camel processes from other checkouts. This can end an
active Camel debug session. Errors are printed and never retried; the commit remains valid.
Invoke `node scripts/sync-editor-runtime.js` explicitly for a manual sync.

The hook checks required binaries, rejects links, copies into a temporary sibling directory,
and verifies file hashes and that the source build did not change during copying. It replaces
the fixed directory only after verification; if installation fails it restores the old directory.
Temporary siblings are removed after success; a failed cleanup is reported without retry.
If the language server does not restart automatically, run **Camel: Restart Language Server**
(**Camel: 重启语言服务器**) from the VS Code command palette.

Disable it with `git config --local --unset camel.runtimeSyncTarget`.
