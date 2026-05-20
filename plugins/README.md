**To create a plugin:**

1. Create `plugins/<name>/` with:
   - `<name>.capnp` — your Cap'n Proto interface
   - `MyPlugin.h` / `MyPlugin.cpp` — C++ Server implementation, inheriting `TestRunnerService::Server`
   - `MyPlugin_Client.h` / `MyPlugin_Client.cpp` — C++ Client implementation, inheriting `RPCClient`
   - CMakeLists.txt - Add plugin schemas and source to CMake configuration

2. Import the interface and extend `Plugins` in `plugins/plugins.capnp`:
   ```capnp
   using import "../plugins/my_plugin/my_plugin.capnp".MyInterface;
   interface Plugins extends (Screenshooter, MyInterface) {}
   ```

3. Extend TestRunnerPlugins (plugins.h) and TestRunnerPluginsClient (plugins_client.h) with corresponding plugin classes 

4. In `plugins/CMakeLists.txt`:
   - Add plugin with `add_subdirectory(MyPlugin)`
   - Add CMake option `ENABLE_PLUGIN_MYPLUGIN` to allow configuration time enable/disable

5. (Optional) In `plugins/plugins.cpp` 
	- Add any initialization steps that can't be done in the constructor to `init_plugins()`

See `plugins/weston_screenshooter/` for a complete example.