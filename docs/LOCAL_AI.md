# Use an AI assistant with the open editor

Apophysis Reborn 0.14.0 can share your selected flame with an AI assistant on the same computer through the Model Context Protocol (MCP). The assistant can inspect unsaved edits, see the live preview, and send Lua suggestions for you to try. Use an AI client that can launch a local **stdio** MCP server. The Windows installer and portable ZIP include the companion, `apophysis-mcp.exe`, beside the editor. The companion needs no Python, API key, or separate account.

## Connect on Windows

1. Start **Apophysis Reborn**, select a flame, and let its preview render.
2. Open **Tools → Local AI connection…** and click **Copy configuration**. The copied JSON includes the exact companion path for this installation.
3. If your AI client uses a JSON file with an `mcpServers` key, paste the copied configuration into an empty file or add just the **apophysis-reborn** entry inside your existing `mcpServers` object. If the client has a form, select **stdio** or **local command**, set **Command** to the full path of `apophysis-mcp.exe`, and leave **Arguments** empty. For VS Code, use the example below.
4. In Apophysis Reborn, click **Enable for this session**, then **Close**. Start or reload the server in your AI client. Allow the Apophysis tools if the client asks.
5. Check the client's tool list for `inspect_flame`, `get_preview` and `propose_script`. Then ask **“Use Apophysis Reborn to tell me the name of the flame I have selected and show me its live preview.”** The name should match the flame selected in the editor, even before you save the file.

**VS Code example.** In a local Windows window, open the Command Palette and choose **MCP: Open User Configuration**. VS Code uses the top-level key `servers`. Put the copied **apophysis-reborn** entry under that key and set its `type` to `stdio`. Start the server from the MCP configuration and select its tools in agent chat. A workspace running over SSH or in a container starts commands in a different environment. [VS Code MCP configuration](https://code.visualstudio.com/docs/agent-customization/mcp-servers)

For example, a portable copy extracted to `C:\Art\Apophysis` would use the following VS Code entry. Use the path from your own editor.

```json
{
  "servers": {
    "apophysis-reborn": {
      "type": "stdio",
      "command": "C:/Art/Apophysis/apophysis-mcp.exe"
    }
  }
}
```

A browser chat by itself cannot launch this Windows helper. An MCP-capable desktop client or local agent supplies that connection. For a fully local workflow, choose a client and model that both run on your computer. A client using a hosted model may send the shared flame or image to that provider.

## Try three variations

> Inspect my selected flame. Duplicate it three times, keeping the original intact. Make one warm copper study, one cool violet study, and one with a tighter spiral. Give them distinct names and propose the edits in one Lua script so I can preview them.

1. The assistant opens a **script review** in Apophysis. Read the proposal and click **Run**. It runs against a working copy of the document.
2. Use the result selector to preview each candidate. The main document still contains your original artwork. Close the review to discard the proposal.
3. Click **Apply result** to put the edited library in the editor and select the result you previewed. A script that adds three copies keeps all three. **Ctrl+Z** undoes the whole proposal. Use **Save as…** to keep a separate project.

## What the connection shares

| Tool | What it does |
| --- | --- |
| `inspect_flame` | Reads the currently selected renderable flame, including unsaved settings, and returns its name, position, library count, flame XML, Lua API reference, and a revision identifier. |
| `get_preview` | Returns a PNG from the editor's current live preview, scaled to at most 384 pixels on its longest side. Requires the revision from inspection. |
| `propose_script` | Opens a Lua proposal for that revision. You control Run and Apply. The review can edit the working library and preview candidate flames. File imports, exports, and queued renders are disabled in AI reviews. |

The editor opens a per-user named pipe on Windows or a local socket on Linux. It uses a fresh session credential and listens only while sharing is enabled. Your AI client configuration contains just the program path. **Tools → Local AI connection… → Disable**, or closing the editor, ends the session. Only one editor can share at a time. Enable sharing again when you reopen the editor.

## If the assistant cannot connect

| Symptom | What to check |
| --- | --- |
| No Apophysis tools appear | Confirm the client's server runs as a local stdio command, start it, and check that its tool list is enabled. If you moved the portable folder or upgraded, copy the new configuration. |
| The tools appear but the editor is unavailable | Open the editor under the same operating-system account, enable sharing for this session, close the settings dialog, and select a renderable flame. |
| The preview is unavailable | Resume the live preview, wait for an image for the current selection, and ask again. |
| The assistant's revision has expired | Ask it to inspect your current flame again. Edits, changes of selection, undo, and reconnecting can invalidate a proposal. |
| There is already an open dialog | Finish or close the current dialog, then request a proposal again. |

To check the companion independently in Windows Git Bash, open a terminal in the installed or extracted application folder and run `./apophysis-mcp.exe --version`. For the supported Lua editing commands, see the [scripting guide](SCRIPTING.md).
