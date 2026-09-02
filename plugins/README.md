**To create a plugin:**

1. Create `plugins/<name>/` with:
   - `<name>.capnp` — your Cap'n Proto interface
   - `MyPlugin.h` / `MyPlugin.cpp` — C++ Server implementation, inheriting `TestRunnerService::Server`
   - `MyPlugin_Client.h` / `MyPlugin_Client.cpp` — C++ Client implementation, inheriting `RPCClient`
   - `CMakeLists.txt` — append plugin sources and link libraries to parent-scope variables

2. Import the interface and extend `Plugins` in `plugins/plugins.capnp`:
   ```capnp
   using import "../plugins/my_plugin/my_plugin.capnp".MyInterface;
   interface Plugins extends (Screenshooter, MyInterface) {}
   ```

3. Guard the plugin class in `plugins/plugins.h` and `plugins/plugins_client.h`
   with `#ifdef ENABLE_PLUGIN_MYPLUGIN` around the include and inheritance.

4. In `cmake/plugins.cmake`:
   - Add `option(ENABLE_PLUGIN_MYPLUGIN "..." ON)` and the matching
     `add_compile_definitions(ENABLE_PLUGIN_MYPLUGIN)` block.

5. In `plugins/CMakeLists.txt`:
   - Add `add_subdirectory(my_plugin)` inside an `if(ENABLE_PLUGIN_MYPLUGIN)` block.
   - Add the plugin's `.capnp` schema to the unconditional
     `TEST_RUNNER_PLUGIN_CAPNP_SCHEMAS` list (schemas are always compiled;
     disabled plugins fall back to capnp's "unimplemented" response).

6. (Optional) In `plugins/plugins.cpp`
	- Add any initialization steps that can't be done in the constructor to `init_plugins()`

See `plugins/weston_screenshooter/` for a complete example.