# 3D world: ACP matrix orbit

The ACP composes and the GPU draws. A 4x4 matrix of float64 places a
pyramid and a rotation multiplies it every frame. So it orbits a second
pyramid placed the ordinary way. The frame is one command byte and a 128
byte copy. Fire swaps the camera between perspective and orthographic
projection. Endless.
