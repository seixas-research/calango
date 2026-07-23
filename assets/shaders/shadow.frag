#version 330 core

// Nothing to write: the depth buffer is the entire product of this pass.
// An empty main() lets the driver keep early-Z and avoids allocating a color
// attachment for the shadow FBO.
void main()
{
}
