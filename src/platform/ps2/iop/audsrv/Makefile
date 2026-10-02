IOP_BIN := audsrv.irx
.DEFAULT_GOAL := all
all: $(IOP_BIN)
IOP_SRC_DIR := src/
IOP_INC_DIR := src/
IOP_OBJS := audsrv.o upsamplers.o hw.o rpc_server.o rpc_client.o common.o cdrom.o imports.o exports.o adpcm.o
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
