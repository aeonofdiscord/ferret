#ferret

*ferret* is a modern client for the [gopher protocol](https://en.wikipedia.org/wiki/Gopher_(protocol)). Currently (0.1.0) only Linux is supported.

##Building from source
First you'll need cmake, which you can get from your package manager or https://cmake.org/.

You'll also need the custom version of libui located here: https://github.com/aeonofdiscord/libui. Hopefully future versions should be able to revert to using the mainline libui.

After that's installed just jump into the directory where you placed the ferret source, and run

	./build.sh