#version 450

// Output variables to pass data to the fragment shader
layout(location = 0) out vec3 fragColor;

// Hardcoded vertex positions for a standard clip-space triangle
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),  // Top vertex
    vec2(0.5, 0.5),   // Bottom right vertex
    vec2(-0.5, 0.5)   // Bottom left vertex
);

// Hardcoded colors for each vertex (Red, Green, Blue)
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // Red
    vec3(0.0, 1.0, 0.0), // Green
    vec3(0.0, 0.0, 1.0)  // Blue
);

void main() {
    // gl_VertexIndex is a built-in variable (0, 1, or 2 for a single triangle)
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    
    // Pass the color of the current vertex to the fragment shader
    fragColor = colors[gl_VertexIndex];
}