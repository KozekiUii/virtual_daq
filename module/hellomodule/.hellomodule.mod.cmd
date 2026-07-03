savedcmd_hellomodule.mod := printf '%s\n'   hellomodule.o | awk '!x[$$0]++ { print("./"$$0) }' > hellomodule.mod
