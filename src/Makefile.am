# 作成する実行可能ファイルの名前
bin_PROGRAMS = pimjpg_srv

pimjpg_srv_LDFLAGS = -lvcos -lbcm_host -lmmal -lmmal_core -lmmal_util -pthread

# Cコンパイラへ渡すオプション(ここではコメントアウトしています)
pimjpg_srv_CFLAGS = -I$(top_srcdir)/inc

# C++コンパイラへ渡すオプション
pimjpg_srv_CXXFLAGS = -I$(top_srcdir)/inc

# test生成に必要なソースコード
pimjpg_srv_SOURCES = main.cc PiBuffer.cc PiCamera.cc PiCameraManager.cc PiFrame.cc PiHttpdInterpreter.cc PiMjpegServer.cc RaspiCamControl.c

