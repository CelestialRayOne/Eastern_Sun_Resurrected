# D2RLoader mod plugins

Put plugin DLLs for this mod in this folder.

D2RLoader will load `.dll` files from here while the mod is active. These plugins are only used by this mod, unless they are also installed globally.

## Building a plugin

Build plugins against the external D2RLoader Plugin SDK:

https://github.com/D2RLoader/PluginSDK

```cpp
#include <D2RLPlugin/api.h>
```

CMake plugins should link the SDK target:

```cmake
target_link_libraries(MyPlugin PRIVATE D2RLPlugin::D2RLPlugin)
```

A plugin must:

* include the D2RLoader plugin manifest resource
* export `D2RLoaderGetPluginInfo`
* export `D2RLoaderLoadPlugin`

`D2RLoaderUnloadPlugin` is optional.

## Plugin info

`D2RLoaderGetPluginInfo` returns basic information about the plugin, such as:

* plugin id
* name
* version
* author
* optional description

The description is shown in the in-game Extensions tab when provided.

Plugin ids may only use lowercase letters, numbers, `.`, `_`, and `-`.

These ids are reserved and cannot be used:

```text
d2rloader
d2rloader.*
```

## Mod plugins vs global plugins

Plugins in this folder load before global plugins from:

```text
<game>/d2rloader/plugins
```

If a mod plugin and a global plugin use the same id, the global plugin will not load while this mod is active.

Use `D2RL::PluginFlags::ModScopedOnly` for plugins that should only ever load from a mod folder.

## Client and server roles

Plugin API v3 requires every plugin to declare exactly one role in its flags:

* `D2RL::PluginFlags::Client`
* `D2RL::PluginFlags::Server`
* `D2RL::PluginFlags::Shared`

The host process performs both roles. Server and shared plugins are recorded
with saved characters. TCP/IP clients must exactly match the host's shared
plugins. Client-only plugins are local and are not saved as character
provenance. Role flags can be combined with `ModScopedOnly` and `NativeHooks`.

Supported Plugin API v2 plugins predate roles. D2RLoader treats them as
`D2RL::PluginFlags::Shared` so they continue to load and match on both sides.

## Plugin context

`D2RLoaderLoadPlugin` receives a `D2RL::PluginContext`.

The context gives the plugin access to D2RLoader features such as:

* logging
* plugin config files
* console commands
* memory patch checks
* runtime memory patches

For plugins in this folder, D2RLoader uses mod-local paths:

```text
mods/<mod>/d2rloader/config/<plugin-id>.toml
mods/<mod>/d2rloader/logs/<plugin-id>.log
```

The context pointer stays valid until the plugin is unloaded during shutdown.

## Console commands

Plugins can register console commands from `D2RLoaderLoadPlugin`.

Example:

```cpp
static auto HelloCommand(
	D2R::Game::Client* client,
	const D2RL::ConsoleCommandContext* command,
	void*
) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;

	command->plugin->WriteConsoleMessage("Hello from my plugin.");
	return D2RL::ConsoleCommandResult::Handled;
}

context->RegisterConsoleCommand(
	"hello-plugin",
	HelloCommand,
	"Print a plugin greeting."
);
```

The `client` pointer is available when there is a local player in an active game. It can be `nullptr` outside a game.

D2RLoader shows the plugin id as the command source automatically.

## Native hooks

Plugins can install raw inline hooks only when they declare:

```cpp
D2RL::PluginFlags::NativeHooks
```

Example:

```cpp
using PotionFn = void(__fastcall*)(void* player, void* item);

static PotionFn OriginalPotion = nullptr;

static void __fastcall PotionHook(void* player, void* item) {
	OriginalPotion(player, item);
}

context->InstallInlineHook(
	potionRva,
	expectedBytes,
	sizeof(expectedBytes),
	PotionHook,
	&OriginalPotion
);
```

Only use native hooks when needed. The hook function and original function pointer must exactly match the target function ABI.

D2RLoader checks the expected bytes before installing the hook and logs native hook installs to `d2rloader.log`.

Prefer D2RLoader-provided typed hooks when they are available.

## Unloading

Keep `D2RLoaderUnloadPlugin` quick and `noexcept`.

Use it to release resources owned by the plugin. D2RLoader calls unload callbacks during shutdown in reverse load order, but plugin DLLs stay loaded for the lifetime of the process.
