# Globed reference and attribution

GlobedGD/globed2 main was inspected at commit
`05fbd11756b81173075178adb8b4c8eefce40e43`.

Sources: `include/globed/core/game/{RemotePlayer,VisualPlayer}.hpp`,
`src/core/game/{RemotePlayer,VisualPlayer}.cpp`, and
`src/core/hooks/{GJBaseGameLayer,PlayLayer}.cpp`.

Corum uses the minimal ownership/presentation split and an object-layer parent.
It uses the native PlayerObject factory instead of Globed's subclass zero-constructor
and manual member initialization. No networking framework, camera, room, voice,
teams, UI or advanced interpolation implementation is imported.

The native companion-node cleanup inventory in CorumRemotePlayer.cpp is adapted
from VisualPlayer::cleanupObjectLayer. The following upstream license is preserved:

MIT License

Copyright (c) 2023 dank_meme

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
