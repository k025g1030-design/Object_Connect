# KamataEngine SDK snapshot

This directory is the project's checked-in, offline build dependency. CMake uses
this snapshot, not a developer's external `Runtime` directory. It is not an engine
source checkout and does not build KamataEngine itself.

## Provenance

- Imported on: 2026-09-10.
- Local input: `D:/code/Runtime/KamataEngine/External`.
- Source Git state: unborn `refs/heads/main`; `git rev-parse --verify HEAD` has no
  commit to resolve. The source directories, including `External/`, were untracked.
- No reliable KamataEngine release number or upstream commit was supplied. Do not
  infer a version from the folder name or filesystem timestamps.
- The checked-in `SHA256SUMS` is the exact content identity for this snapshot. It
  covers all files here with paths relative to this directory, except itself.
  `.gitattributes` disables newline conversion so Git checkouts preserve the
  recorded bytes, including original third-party header formatting.

## Included files

- `External/KamataEngine/include/`: all supplied headers.
- `External/KamataEngine/lib/{Debug,Release}/`: the matching `.lib` and `.pdb`.
- `External/DirectXTex/include/`: all supplied headers and inline implementation
  files, including the DirectX helper header used by the engine.
- `External/DirectXTex/lib/{Debug,Release}/`: the matching `.lib` and `.pdb`.
- `External/imgui/`: all supplied `.h` files and the original `LICENSE.txt`.

The imported SDK consists of 58 byte-for-byte copies (75,731,513 bytes), excluding
the documentation and release notices added here. No examples, `.git`, `.idb`,
Develop libraries, ImGui `.cpp` files, or duplicate game resources are included.
These files use ordinary Git, not Git LFS or a submodule.

The PDBs are build inputs for symbol resolution and debugging and stay next to
their libraries. They are not player assets. Debug uses the supplied SDK's `/MDd`
runtime model; Release uses `/MT`. The build still requires compatible x64 MSVC
and Windows SDK tools. The tested toolchain family is Visual Studio 2026.

## Notices and distribution

Keep all existing copyright notices in the headers. `LICENSES/` is copied into the
player package by the packaging script. It contains the original ImGui license
and the MIT license texts verified from Microsoft's official DirectXTex and
DirectX-Headers repositories. The original SDK did not include separate copies of
the Microsoft license texts; their supplied headers already identify the MIT
license. Source URLs are recorded in `LICENSES/README.md`.

No KamataEngine license text was present in the supplied snapshot. No permission
or license is invented by importing it here: the project maintainer remains
responsible for retaining the engine owner's applicable redistribution permission.
The Microsoft and ImGui licenses do not license KamataEngine itself.

## Updating the snapshot

Replace headers, libraries and matching PDBs together from one compatible SDK.
Record the actual source commit/release if available, otherwise record that it is
unknown; update the original-copy inventory and retain all applicable notices.
Regenerate `SHA256SUMS` in ordinal path order using lowercase SHA-256, two spaces,
then the forward-slash relative path (UTF-8 without BOM). Verify both Debug and
Release with the VS2026 and Ninja builds before merging the dependency update.
Do not reintroduce local absolute paths or silently download a newer SDK in CI.

For normal build and release commands, see the project [README](../../README.md).
