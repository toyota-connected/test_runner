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
   - Add `my_plugin` to the `TEST_RUNNER_PLUGINS` list. The `ENABLE_PLUGIN_MY_PLUGIN`
     option, `add_compile_definitions`, `add_subdirectory`, and capnp schema collection
     are all driven automatically from this list.

6. (Optional) In `plugins/plugins.cpp`
	- Add any initialization steps that can't be done in the constructor to `init_plugins()`

See `plugins/wayland_screenshooter/` for a complete example.