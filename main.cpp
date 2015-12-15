#ifdef WIN32
#include <windows.h>
#endif


#include <GL/glew.h>
#include <GL/freeglut.h>

#include <IL/il.h>
#include <IL/ilut.h>

#include <stdlib.h>
#include <algorithm>

#include <OBJModel.h>
#include <glutil.h>
#include <float4x4.h>
#include <float3x3.h>

using namespace std;
using namespace chag;

//*****************************************************************************
//	Global variables
//*****************************************************************************
bool paused = false;				// Tells us wether sun animation is paused
float currentTime = 0.0f;		// Tells us the current time
GLuint shaderProgram;
const float3 up = {0.0f, 1.0f, 0.0f};

//*****************************************************************************
//	OBJ Model declarations
//*****************************************************************************
OBJModel *world; 
OBJModel *water; 
OBJModel *skybox; 
OBJModel *skyboxnight; 
OBJModel *car; 

//*****************************************************************************
//	Camera state variables (updated in motion())
//*****************************************************************************
float camera_theta = M_PI / 6.0f;
float camera_phi = M_PI / 4.0f;
float camera_r = 30.0; 
float camera_target_altitude = 5.2; 

//*****************************************************************************
//	Light state variables (updated in idle())
//*****************************************************************************
float3 lightPosition = {30.1f, 450.0f, 0.1f};

//*****************************************************************************
//	Mouse input state variables
//*****************************************************************************
bool leftDown = false;
bool middleDown = false;
bool rightDown = false;
int prev_x = 0;
int prev_y = 0;

// Shader used to draw the shadow map (and some other simple objects)
GLuint basicShaderProgram;
GLuint shadowMapTexture;
GLuint shadowMapFBO;
GLuint cubeMapTexture;
const int shadowMapResolution = 1024;

float4x4 lightViewMatrix;
float4x4 lightProjMatrix;


GLuint cubeMapFBO;
GLuint cubeMapDepth;


// Helper function to turn spherical coordinates into cartesian (x,y,z)
float3 sphericalToCartesian(float theta, float phi, float r)
{
	return make_vector( r * sinf(theta)*sinf(phi),
					 	r * cosf(phi), 
						r * cosf(theta)*sinf(phi) );
}


void initGL()
{
	/* Initialize GLEW; this gives us access to OpenGL Extensions.
	 */
	glewInit();  

	/* Print information about OpenGL and ensure that we've got at a context 
	 * that supports least OpenGL 3.0. Then setup the OpenGL Debug message
	 * mechanism.
	 */
	startupGLDiagnostics();
	setupGLDebugMessages();

	/* Initialize DevIL, the image library that we use to load textures. Also
	 * tell IL that we intent to use it with OpenGL.
	 */
	ilInit();
	ilutRenderer(ILUT_OPENGL);

	/* Workaround for AMD. It might no longer be necessary, but I dunno if we
	 * are ever going to remove it. (Consider it a piece of living history.)
	 */
	if( !glBindFragDataLocation )
	{
		glBindFragDataLocation = glBindFragDataLocationEXT;
	}

	/* As a general rule, you shouldn't need to change anything before this 
	 * comment in initGL().
	 */

	//*************************************************************************
	//	Load shaders
	//*************************************************************************
	shaderProgram = loadShaderProgram("simple.vert", "simple.frag");
	glBindAttribLocation(shaderProgram, 0, "position"); 	
	glBindAttribLocation(shaderProgram, 2, "texCoordIn");
	glBindAttribLocation(shaderProgram, 1, "normalIn");
	glBindFragDataLocation(shaderProgram, 0, "fragmentColor");
	linkShaderProgram(shaderProgram);


	basicShaderProgram = loadShaderProgram("basic.vert", "basic.frag");
	glBindAttribLocation(basicShaderProgram, 0, "position");
	glBindFragDataLocation(basicShaderProgram, 0, "fragmentColor");
	linkShaderProgram(basicShaderProgram);

	//*************************************************************************
	// Load the models from disk
	//*************************************************************************
	world = new OBJModel(); 
	world->load("../scenes/world.obj");
	skybox = new OBJModel();
	skybox->load("../scenes/skybox.obj");
	skyboxnight = new OBJModel();
	skyboxnight->load("../scenes/skyboxnight.obj");
	// Make the textures of the skyboxes use clamp to edge to avoid seams
	for(int i=0; i<6; i++){
		glBindTexture(GL_TEXTURE_2D, skybox->getDiffuseTexture(i)); 
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, skyboxnight->getDiffuseTexture(i)); 
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	water = new OBJModel(); 
	water->load("../scenes/water.obj");
	car = new OBJModel(); 
	car->load("../scenes/car.obj");


	//Cube map
	glGenTextures(1, &cubeMapTexture);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);

	const int size = 128;
	// create the fbo
	glGenFramebuffers(1, &cubeMapFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, cubeMapFBO);

	for (int i = 0; i < 6; i++)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
			size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

	}
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
		GL_LINEAR);
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
		GL_LINEAR);
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S,
		GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T,
		GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R,
		GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
	
	// create the uniform depth buffer
	glGenRenderbuffers(1, &cubeMapDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, cubeMapDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size, size);
	//glBindRenderbuffer(GL_RENDERBUFFER, 0);
	// attach it
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cubeMapFBO);
	//glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, cubeMapFBO, 0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	////////

	// Cleanup: activate the default frame buffer again
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	// Generate and bind our shadow map texture
	glGenTextures(1, &shadowMapTexture);
	glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
	// Specify the shadow map texture’s format: GL_DEPTH_COMPONENT[32] is
	// for depth buffers/textures.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32,
		shadowMapResolution, shadowMapResolution, 0,
		GL_DEPTH_COMPONENT, GL_FLOAT, 0
		);
	// We need to setup these; otherwise the texture is illegal as a
	// render target.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	float4 zeros = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &zeros.x);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
		GL_COMPARE_REF_TO_TEXTURE);

	// Cleanup: unbind the texture again - we’re finished with it for now
	glBindTexture(GL_TEXTURE_2D, 0);
	// Generate and bind our shadow map frame buffer
	glGenFramebuffers(1, &shadowMapFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
	// Bind the depth texture we just created to the FBO’s depth attachment
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_TEXTURE_2D, shadowMapTexture, 0);

	// We’re rendering depth only, so make sure we’re not trying to access
	// the color buffer by setting glDrawBuffer() and glReadBuffer() to GL_NONE
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	lightProjMatrix = perspectiveMatrix(22.0f, 1.0f, 0.1f, 1000.0f);
	/*
	cubeMapTexture = loadCubeMap("cube0.png", "cube1.png",
		"cube2.png", "cube3.png",
		"cube4.png", "cube5.png");
		*/
	glUseProgram(shaderProgram);
	setUniformSlow(shaderProgram, "environmentMap", 2);

}


void drawModel(OBJModel *model, const float4x4 &modelMatrix)
{
	GLint currentProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

	setUniformSlow(currentProgram, "modelMatrix", modelMatrix);
	model->render();
}

/**
* In this function, add all scene elements that should cast shadow, that way
* there is only one draw call to each of these, as this function is called twice.
*/
void drawShadowCasters()
{
	GLint currentProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

	drawModel(world, make_identity<float4x4>());
	setUniformSlow(currentProgram, "object_reflectiveness", 0.5f);
	drawModel(car, make_translation(make_vector(0.0f, 0.0f, 0.0f)));
	setUniformSlow(currentProgram, "object_reflectiveness", 0.0f);
}

void drawScene(void)
{
	glEnable(GL_DEPTH_TEST);	// enable Z-buffering 

	// enable back face culling.
	glEnable(GL_CULL_FACE);	

	//*************************************************************************
	// Render the scene from the cameras viewpoint, to the default framebuffer
	//*************************************************************************
	glClearColor(0.2,0.2,0.8,1.0);						
	glClearDepth(1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
	int w = glutGet((GLenum)GLUT_WINDOW_WIDTH);
	int h = glutGet((GLenum)GLUT_WINDOW_HEIGHT);
	glViewport(0, 0, w, h);								
	// Use shader and set up uniforms
	glUseProgram( shaderProgram );			
	float3 camera_position = sphericalToCartesian(camera_theta, camera_phi, camera_r);
	float3 camera_lookAt = make_vector(0.0f, camera_target_altitude, 0.0f);
	float3 camera_up = make_vector(0.0f, 1.0f, 0.0f);
	float4x4 viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
	float4x4 projectionMatrix = perspectiveMatrix(45.0f, float(w) / float(h), 0.1f, 1000.0f);
	setUniformSlow(shaderProgram, "viewMatrix", viewMatrix);
	setUniformSlow(shaderProgram, "inverseViewNormalMatrix",
		transpose(viewMatrix));
	setUniformSlow(shaderProgram, "projectionMatrix", projectionMatrix);
	setUniformSlow(shaderProgram, "lightpos", lightPosition); 

	float4x4 lightMatrix = lightProjMatrix * lightViewMatrix * inverse(viewMatrix);

	setUniformSlow(shaderProgram, "lightMatrix", lightMatrix);


	drawModel(water, make_translation(make_vector(0.0f, -6.0f, 0.0f)));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
	setUniformSlow(shaderProgram, "shadowMap", 1);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
	setUniformSlow(shaderProgram, "environmentMap", 2);

	float3 viewSpaceLightDir = transformDirection(
		viewMatrix, -normalize(lightPosition));
	setUniformSlow(shaderProgram, "viewSpaceLightDir", viewSpaceLightDir);



	drawShadowCasters();

	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	drawModel(skyboxnight, make_identity<float4x4>());
	setUniformSlow(shaderProgram, "object_alpha", max<float>(0.0f, cosf((currentTime / 20.0f) * 2.0f * M_PI))); 
	drawModel(skybox, make_identity<float4x4>());
	setUniformSlow(shaderProgram, "object_alpha", 1.0f); 
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE); 

	glUseProgram( 0 );	
}

void drawShadowMap()
{
	glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO),
	glViewport(0, 0, shadowMapResolution, shadowMapResolution);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClearDepth(1.0);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1.0, 2);

	// Get current shader, so we can restore it afterwards. Also, switch to
	// the simple shader used to draw the shadow map.
	GLint currentProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
	glUseProgram(basicShaderProgram);

	setUniformSlow(basicShaderProgram, "projectionMatrix", lightProjMatrix);
	setUniformSlow(basicShaderProgram, "viewMatrix", lightViewMatrix);
	// draw shadow casters
	drawShadowCasters();

	// Restore old shader
	glUseProgram(currentProgram);

	glDisable(GL_POLYGON_OFFSET_FILL);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void drawCubeMap()
{
	
	glBindFramebuffer(GL_FRAMEBUFFER, cubeMapFBO);
	glViewport(0, 0, 128, 128);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClearDepth(1.0);
	glEnable(GL_DEPTH_TEST);// enable Z-buffering
	glEnable(GL_CULL_FACE);// enable back face culling.
	
	glUseProgram(shaderProgram);
	
	for (int i = 0; i<6; i++)
	{
		if (i == 0) { //X+
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X, cubeMapTexture, 0);
		}
		else if (i == 1) { //X-
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_NEGATIVE_X, cubeMapTexture, 0);
		}
		else if (i == 2) {//Y+
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_Y, cubeMapTexture, 0);
		}
		else if (i == 3) {//....
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, cubeMapTexture, 0);
		}
		else if (i == 4) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_Z, cubeMapTexture, 0);
		}
		else if (i == 5) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, cubeMapTexture, 0);
		}
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float cameraYOffset = 1.0f;
		float3 camera_position = make_vector(0.0f, cameraYOffset, 0.0f);
		float3 camera_lookAt, camera_up;
		float4x4 viewMatrix;
		if (i == 0) { //X+
			//cam->Update(vec3(0), vec3(10, 0, 0)); // position, target

			camera_up = make_vector(0.0f, -1.0f, 0.0f);
			camera_lookAt = make_vector(1.0f, cameraYOffset, 0.0f);
			viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
		}
		else if (i == 1) { //X-
			//cam->Update(vec3(0), vec3(-10, 0, 0));
			camera_up = make_vector(0.0f, -1.0f, 0.0f);
			camera_lookAt = make_vector(-1.0f, cameraYOffset, 0.0f);
			viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
		}
		else if (i == 2) {//Y+
			//cam->Update(vec3(0), vec3(0, 10, 0));
			camera_up = make_vector(0.0f, 0.0f, 1.0f);
			camera_lookAt = make_vector(0.0f, 1.0f + cameraYOffset, 0.0f);
			viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
		}
		else if (i == 3) {//....
			//cam->Update(vec3(0), vec3(0, -10, 0));
			camera_up = make_vector(0.0f, 0.0f, 1.0f);
			camera_lookAt = make_vector(0.0f, -1.0f + cameraYOffset, 0.0f);
			viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
		}
		else if (i == 4) {
			//cam->Update(vec3(0), vec3(0, 0, 10));
			camera_up = make_vector(0.0f, -1.0f, 0.0f);
			camera_lookAt = make_vector(0.0f, cameraYOffset, 1.0f);
			viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
		}
		else if (i == 5) {
			//cam->Update(vec3(0), vec3(0, 0, -10));
			camera_up = make_vector(0.0f, -1.0f, 0.0f);
			camera_lookAt = make_vector(0.0f, cameraYOffset, -1.0f);
			viewMatrix = lookAt(camera_position, camera_lookAt, camera_up);
		}
		
		float4x4 projectionMatrix = perspectiveMatrix(90.0f, 1.0f, 0.1f, 1000.0f);
		setUniformSlow(shaderProgram, "viewMatrix", viewMatrix);
		setUniformSlow(shaderProgram, "inverseViewNormalMatrix",
			transpose(viewMatrix));
		setUniformSlow(shaderProgram, "projectionMatrix", projectionMatrix);
		setUniformSlow(shaderProgram, "lightpos", lightPosition);

		float4x4 lightMatrix = lightProjMatrix * lightViewMatrix * inverse(viewMatrix);

		setUniformSlow(shaderProgram, "lightMatrix", lightMatrix);

		drawModel(water, make_translation(make_vector(0.0f, -6.0f, 0.0f)));

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
		setUniformSlow(shaderProgram, "shadowMap", 1);

		float3 viewSpaceLightDir = transformDirection(
			viewMatrix, -normalize(lightPosition));
		setUniformSlow(shaderProgram, "viewSpaceLightDir", viewSpaceLightDir);

		drawModel(world, make_identity<float4x4>());
		
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		drawModel(skyboxnight, make_identity<float4x4>());
		setUniformSlow(shaderProgram, "object_alpha", max<float>(0.0f, cosf((currentTime / 20.0f) * 2.0f * M_PI)));
		drawModel(skybox, make_identity<float4x4>());
		setUniformSlow(shaderProgram, "object_alpha", 1.0f);
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		
		
	}

	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}



void display(void)
{	
	// construct light matrices
	drawShadowMap();
	
	drawCubeMap();

	drawScene();
	glutSwapBuffers();  // swap front and back buffer. This frame will now be displayed.
	CHECK_GL_ERROR();
}



void handleKeys(unsigned char key, int /*x*/, int /*y*/)
{
	switch(key)
	{
	case 27:    /* ESC */
		exit(0); /* dirty exit */
		break;   /* unnecessary, I know */
	case 32:    /* space */
		paused = !paused;
		middleDown = !middleDown;
		break;
	case 122:
		break;
	}
}



void handleSpecialKeys(int key, int /*x*/, int /*y*/)
{
	switch(key)
	{
	case GLUT_KEY_LEFT:
		printf("Left arrow\n");
		break;
	case GLUT_KEY_RIGHT:
		printf("Right arrow\n");
		break;
	case GLUT_KEY_UP:
	case GLUT_KEY_DOWN:
		break;
	}
}



void mouse(int button, int state, int x, int y)
{
	// reset the previous position, such that we only get movement performed after the button
	// was pressed.
	prev_x = x;
	prev_y = y;

	bool buttonDown = state == GLUT_DOWN;

	switch(button)
	{
	case GLUT_LEFT_BUTTON:
		leftDown = buttonDown;
		break;
	case GLUT_MIDDLE_BUTTON:
		middleDown = buttonDown;
		break;
	case GLUT_RIGHT_BUTTON: 
		rightDown = buttonDown;
	default:
		break;
	}
}

void motion(int x, int y)
{
	int delta_x = x - prev_x;
	int delta_y = y - prev_y;

	if(middleDown)
	{
		camera_r -= float(delta_y) * 0.3f;
		// make sure cameraDistance does not become too small
		camera_r = max(0.1f, camera_r);
	}
	if(leftDown)
	{
		camera_phi	-= float(delta_y) * 0.3f * float(M_PI) / 180.0f;
		camera_phi = min(max(0.01f, camera_phi), float(M_PI) - 0.01f);
		camera_theta -= float(delta_x) * 0.3f * float(M_PI) / 180.0f;
	}

	if(rightDown)
	{
		camera_target_altitude += float(delta_y) * 0.1f; 
	}
	prev_x = x;
	prev_y = y;
}



void idle( void )
{
	static float startTime = float(glutGet(GLUT_ELAPSED_TIME)) / 1000.0f;
	// Here is a good place to put application logic.
	if (!paused)
	{
		currentTime = float(glutGet(GLUT_ELAPSED_TIME)) / 1000.0f - startTime;
	}

	// rotate light around X axis, sunlike fashion.
	// do one full revolution every 20 seconds.
	float4x4 rotateLight = make_rotation_x<float4x4>(2.0f * M_PI * currentTime / 20.0f);
	// rotate and update global light position.
	lightPosition = make_vector3(rotateLight * make_vector(30.1f, 450.0f, 0.1f, 1.0f));

	lightViewMatrix = lookAt(lightPosition, make_vector(0.0f, 0.0f, 0.0f), make_vector(0.0f, 1.0f, 0.0f));

	glutPostRedisplay();  
	// Uncommenting the line above tells glut that the window 
	// needs to be redisplayed again. This forces the display to be redrawn
	// over and over again. 
}

int main(int argc, char *argv[])
{
#	if defined(__linux__)
	linux_initialize_cwd();
#	endif // ! __linux__

	glutInit(&argc, argv);

	/* Request a double buffered window, with a sRGB color buffer, and a depth
	 * buffer. Also, request the initial window size to be 800 x 600.
	 *
	 * Note: not all versions of GLUT define GLUT_SRGB; fall back to "normal"
	 * RGB for those versions.
	 */
#	if defined(GLUT_SRGB)
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_SRGB | GLUT_DEPTH);
#	else // !GLUT_SRGB
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	printf( "--\n" );
	printf( "-- WARNING: your GLUT doesn't support sRGB / GLUT_SRGB\n" );
#	endif // ~ GLUT_SRGB
	glutInitWindowSize(800,600);

	/* Require at least OpenGL 3.0. Also request a Debug Context, which allows
	 * us to use the Debug Message API for a somewhat more humane debugging
	 * experience.
	 */
	glutInitContextVersion(3,0);
	glutInitContextFlags(GLUT_DEBUG);

	/* Request window
	 */
	glutCreateWindow("Project");

	/* Set callbacks that respond to various events. Most of these should be
	 * rather self-explanatory (i.e., the MouseFunc is called in response to
	 * a mouse button press/release). The most important callbacks are however
	 *
	 *   - glutDisplayFunc : called whenever the window is to be redrawn
	 *   - glutIdleFunc : called repeatedly
	 *
	 * The window is redrawn once at startup (at the beginning of
	 * glutMainLoop()), and whenever the window changes (overlap, resize, ...).
	 * To repeatedly redraw the window, we need to manually request that via
	 * glutPostRedisplay(). We call this from the glutIdleFunc.
	 */
	glutIdleFunc(idle);
	glutDisplayFunc(display);

	glutKeyboardFunc(handleKeys); // standard key is pressed/released
	glutSpecialFunc(handleSpecialKeys); // "special" key is pressed/released
	glutMouseFunc(mouse); // mouse button pressed/released
	glutMotionFunc(motion); // mouse moved *while* any button is pressed

	/* Now that we should have a valid GL context, perform our OpenGL 
	 * initialization, before we enter glutMainLoop().
	 */
	initGL();

	/* If sRGB is available, enable rendering in sRGB. Note: we should do
	 * this *after* initGL(), since initGL() initializes GLEW.
	 */
	glEnable(GL_FRAMEBUFFER_SRGB);

	/* Start the main loop. Note: depending on your GLUT version, glutMainLoop()
	 * may never return, but only exit via std::exit(0) or a similar method.
	 */
	glutMainLoop();


	return 0;          
}
