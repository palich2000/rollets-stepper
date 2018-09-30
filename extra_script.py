Import("env")

my_flags = env.ParseFlags(env['BUILD_FLAGS'])

defines = {k: v for (k, v) in my_flags.get("CPPDEFINES")}

#print env.Dump()

env.Replace(PROGNAME="firmware-%s-%s-%s" % (env['PIOENV'], env['BOARD'], defines.get("VERSION")))
