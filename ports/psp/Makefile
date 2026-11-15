TARGET = openrbx_psp
LUA_DIR = vendor/lua-5.1.1/src
LUA_UNITS = lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lmathlib ltablib lstrlib
OBJS = main.o scene.o runtime.o schema.o math3d.o collision.o physics.o player.o script.o script_values.o $(addprefix lua/,$(addsuffix .o,$(LUA_UNITS)))
BUILD_PRX = 1
PSP_LARGE_MEMORY = 1
PSP_FW_VERSION = 200
INCDIR = . $(LUA_DIR)
CFLAGS = -O2 -G0 -Wall -Wextra -MMD -MP
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-rtti
LIBS = -lpspgum_vfpu -lpspvfpu -lpspgu -lpsppower -lm -lstdc++
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = OpenRBX PSP - Runtime WIP
PSPSDK = $(shell psp-config -p)
include $(PSPSDK)/lib/build.mak

# Track portable runtime headers too; the SDK's default rules do not do this.
# Otherwise adding an Instance/DataModel field can silently retain old ABI users.
$(OBJS): Makefile
-include $(OBJS:.o=.d)

# build.mak does not otherwise refresh PARAM.SFO when memory flags change.
PARAM.SFO: Makefile

vendor/.lua-staged: tools/stage_lua.py $(wildcard ../../Client/App/lua-5.1.1/src/*.[ch])
	python3 tools/stage_lua.py

$(LUA_DIR)/%.c: vendor/.lua-staged
	@test -f $@

script.o script_values.o: vendor/.lua-staged $(wildcard $(LUA_DIR)/*.h)

lua/%.o: $(LUA_DIR)/%.c vendor/.lua-staged $(wildcard $(LUA_DIR)/*.h)
	@mkdir -p lua
	$(CXX) $(CXXFLAGS) -x c++ -I$(LUA_DIR) -c $< -o $@
