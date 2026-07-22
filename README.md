#VBAN UTILS

Here are CLI VBAN receptor and emitter for Jack (Linux/Mac) and Pipewire (Linux Only)
They have NO autoconnect option (use qpwgraph, qjackctl, helvum, ray-session and others to connect)
to have access to ALL channels. Just run them with -h key for help (they are like Benoit's utils).

Main differences from other authors' utils:
1. RECEPTOR HAS LIBZITA RESAMPLER BY FONS ADRIAENSEN!!!
2. Receptors have singlestream and multistream modes.
   In multistream mode they will receive all the streams on the UDP port,
   and SAMPLERATE DOES NOT MATTER (see p.1).
3. In single stream mode is not necessary to give both Streamname and IP,
   If you give IP only - it will start receiving the 1st stream from this IP, which found,
   If you give Streamname only - it will start receiving 1st stream with this Streamname.

We use them in on-stage mode.

To build just run build.sh script (it will create directory vban_utils and put results there)
or classical make (in each directory):

make
sudo make install
make clean

Enjoy! They just work!

Also you can use unix-pipes (named, stdin/stdout) instead of network sockets
(VBAN packet format presents).

ALSA UTIL (thanks for Fons Adriaensen again) - instruction for build and run coming soon!

TODO:
1. Virtual soundcards for Mac OS (also coming soon, thanks to Victor Gaydov),
2. GUI versions or manager (if anybody helps - it will be really cool!),
3. LV2 vanilla plugins with WebGUI.

See also https://github.com/billythehippo/VBANEmitter.vst3 and https://github.com/billythehippo/VBANReceptor.vst3
