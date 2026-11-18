# Native project format, version 1

Apophysis Reborn uses `.apoproject` for a complete working project. It is a UTF-8
XML envelope containing the original flame or flame collection, application
settings, named render presets, metadata, plugin references and an optional
opaque animation payload. It is implemented in the Qt-independent core in
`src/project_io.hpp` and `src/project_io.cpp`.

The embedded flame collection is authoritative. It includes the palettes,
transform definitions, unknown variation parameters and original producer
attributes. Its bytes are base64 encoded without converting or normalizing the
original XML. A UTF-16 collection, comments, CRLF line endings, declarations and
unrenderable plugin-dependent entries survive a project round trip exactly.
Opening the project validates the legacy collection structure; it does not
require every entry to be renderable.

## Envelope

```xml
<?xml version="1.0" encoding="UTF-8"?>
<apophysis-project version="1" selected="0">
  <flame-library encoding="base64">BASE64_OF_ORIGINAL_FILE_BYTES</flame-library>
  <render>
    <setting key="outputSeed" value="18446744073709551615"/>
  </render>
  <preview>
    <setting key="samples" value="400000"/>
  </preview>
  <workspace>
    <setting key="layout" value="APPLICATION_OWNED_VALUE"/>
  </workspace>
  <metadata>
    <setting key="author" value="Artist name"/>
  </metadata>
  <render-presets>
    <preset name="Poster">
      <setting key="outputWidth" value="8192"/>
      <setting key="outputHeight" value="6144"/>
    </preset>
  </render-presets>
  <plugins>
    <plugin id="example-wave" version="1" path="example.apoplugin"
            fingerprint="APPLICATION_OWNED_FINGERPRINT">
      <setting key="note" value="Informational dependency reference"/>
    </plugin>
  </plugins>
  <animation encoding="base64">BASE64_OF_OPAQUE_BYTES</animation>
</apophysis-project>
```

`version` must be `1`. Other versions are rejected with the encountered version
in the error message; the reader never assumes a newer file is compatible.
`selected` is a zero-based flame index and defaults to zero when absent. It must
refer to an existing entry. Exactly one `flame-library` is required. Other known
sections are optional and can occur at most once.

The four setting maps have UTF-8 string keys and string values. The application
that owns a key validates its meaning and numeric range. In particular, 64-bit
render seeds remain decimal strings rather than passing through floating point.
Unknown keys remain available and are saved. Setting keys must be unique in each
map. Named preset and plugin-reference collections are keyed by preset name and
plugin ID respectively; their order has no defined meaning. Changing a name or
ID means removing that named item and adding another.

Plugin paths, IDs, versions and fingerprints describe dependencies. Reading a
project never loads, installs or enables native code. The plugin manager retains
its separate explicit enablement boundary. A project reference alone is not a
trust decision.

The project envelope preserves animation bytes independently of their meaning. M9
adds a strict `apophysis-animation` version-1 schema, interpreted only by the
animation module; see [ANIMATION_MODEL.md](ANIMATION_MODEL.md). Unknown payloads
and future versions remain opaque and survive project saves. The timeline editor
refuses implicit replacement, while the CLI requires `--replace-animation` for
intentional replacement. There is no compressed archive or decompression step.

## Preservation on edits

The parsed `ProjectFile` holds an immutable source snapshot alongside its public
fields. Keep that object, including `retained_source`, when editing or copying a
project. Copies can safely serve as undo snapshots.

An untouched parsed project serializes to exactly its original bytes. Changes
patch only the relevant spans of the original XML:

- Replacing the legacy collection updates its base64 payload.
- Changing a setting edits its `value` attribute, preserving other attributes,
  quotes, comments, unknown children and adjacent XML.
- Adding a setting or a known optional section appends it without rewriting
  unrelated sections. Self-closing sections can expand while retaining their
  attributes.
- Editing existing presets and plugin references preserves their unrecognized
  attributes and children. Unknown sibling elements and root extensions remain
  intact.
- Explicitly deleting a named setting, preset, plugin reference or animation
  payload removes that item, including extensions attached to the deleted item.

A deliberate payload replacement replaces the payload body, including comments
inside that payload. Other envelope metadata is unaffected. Unknown elements are
preserved, not executed or assigned new semantics. Namespace-qualified extension
elements are supported as opaque XML; the core reserves its unqualified known
names.

Fresh projects use canonical readable envelope formatting. Original collections
are never regenerated simply because they are wrapped in a project. Extracting
a collection returns its current original-encoding bytes.

## Core API

`parse_project(bytes)` and `load_project(path)` return `ProjectFile`.
`serialize_project(project)` returns the complete envelope; `save_project(project,
path)` writes it using the existing atomic-output helper. Serialization validates
the project before opening the destination, and failed validation leaves the
previous file intact. All failures use exceptions with a `Project:` explanation.

The desktop document session and command-line project operations use the same
core. CLI examples:

```sh
flame-cpu project create examples/ember-spiral.flame --output ember.apoproject
flame-cpu project inspect ember.apoproject
flame-cpu project extract ember.apoproject --output ember-restored.flame
```

Existing output protection follows the CLI's `--overwrite` option. Saving a
project as a plain flame collection carries the flame content; workspace,
render presets and project-only metadata belong to the native envelope.

## Bounds and invalid input

| Resource | Limit or rule |
|---|---|
| Complete envelope | 96 MiB |
| Decoded flame collection | 64 MiB, nonempty |
| Decoded opaque animation data | 8 MiB |
| XML depth | 32 levels |
| XML elements | 65,536 |
| Settings in one map | 4,096 |
| Setting key | Nonempty, at most 4,096 bytes |
| Setting value | At most 1 MiB |
| Named render presets | 256, with unique nonempty names |
| Plugin references | 4,096, with unique nonempty IDs |
| Base64 | Strict alphabet, padding and decoded-size checks |
| Envelope encoding | UTF-8 |
| DTDs and declared entities | Rejected |

Built-in XML escaping is supported. External files, external entities and native
modules are never resolved by the parser. Invalid UTF-8, XML control characters,
duplicate known sections, duplicate setting keys, unknown payload encodings and
out-of-range selections fail explicitly.

## Verification

`project-io-tests` checks byte-exact UTF-16 collection and untouched-envelope
round trips, opaque unknown metadata through targeted edits, self-closing
section expansion, presets and plugin references, 64-bit seed preservation,
opaque animation bytes, invalid versions, duplicate sections/keys, bad base64,
entity declarations, excessive nesting and atomic-save failure preservation.
The editor's document-session tests additionally cover project integration.

## Animation content and undo in M9

`DocumentSession::changeAnimation` validates the complete project envelope before
pushing one content-undo command. Undo restores the animation payload while
retaining current library bytes and unrelated project preferences. The ordinary
clean marker tracks timeline edits. Save, conflict detection and interrupted-session
recovery include the entire native envelope.

A document containing any animation payload cannot be saved directly as a plain
`.flame`/`.xml` file. The UI prompts for `.apoproject` and offers a separate
**Export flame library** action. Exact source bytes in that exported copy exclude
the project envelope and timeline. Timeline keyframes retain their own source
collections, selected entries, correspondence IDs and captured module identities.
Plugin references used only by recognized animation keyframes are also recorded
when the editor captures project state; recording them does not enable code.
