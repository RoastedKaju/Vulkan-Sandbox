#version 450

// Input variable coming from the vertex shader (automatically interpolated!)
layout(location = 0) in vec3 fragColor;

// The final output color of the pixel on the screen
layout(location = 0) out vec4 outColor;

void main() {
    // Output the interpolated color with an alpha (opacity) of 1.0
    outColor = vec4(fragColor, 1.0);
}