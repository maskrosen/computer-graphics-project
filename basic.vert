#version 130

in vec3 position;
uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;
uniform vec3 lightpos; 

void main()
{
	gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1);
}
