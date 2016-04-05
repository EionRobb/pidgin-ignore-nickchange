pidgin-ignore-nickchange
========================

Pidgin plugin to ignore "X is now known as Y" messages in chat

The name of this plugin (under which it dhows up in the Plugins list) is "Nick Change Hiding", and not "Ignore Nick Change" as listed on https://developer.pidgin.im/wiki/ThirdPartyPlugins#Notifications.

Windows plugin download from http://eion.robbmob.com/nickchange.dll

To compile on Linux, make sure you have the 'glib-dev', 'pidgin-dev', 'purple-dev' and 'pkg-config' packages for your distro and run
```bash
gcc -Wall -fPIC nickchange.c -o nickchange.so -shared `pkg-config --cflags --libs glib-2.0 purple pidgin`
```

then copy the `nickchange.so` file to your `~/.purple/plugins` folder.

Or run
```bash
make
```

which does all that for you.
