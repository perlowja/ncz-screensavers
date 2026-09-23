/*
 * testsaver - minimal saver verifying the rss-port stack:
 * SDL2 window -> ANGLE GLES2 -> gl4es (immediate mode, matrix stacks,
 * lighting, display lists) -> glues (gluPerspective, gluSphere).
 *
 * Uses the exact same hook contract as the real savers.
 */

#ifdef RS_XSCREENSAVER
#include <rsXScreenSaver/rsXScreenSaver.h>
#endif

#include <stdio.h>
#include <math.h>
#include <GL/gl.h>
#include <GL/glu.h>

static float angle = 0.0f;
static float frameTime = 0.0f;
static float aspectRatio = 1.0f;
static int readyToDraw = 0;

void draw()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -4.0f);

    // Spinning immediate-mode triangle
    glPushMatrix();
    glTranslatef(-1.0f, 0.0f, 0.0f);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glDisable(GL_LIGHTING);
    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.2f, 0.2f); glVertex3f(0.0f, 1.0f, 0.0f);
        glColor3f(0.2f, 1.0f, 0.2f); glVertex3f(-0.9f, -0.7f, 0.0f);
        glColor3f(0.2f, 0.4f, 1.0f); glVertex3f(0.9f, -0.7f, 0.0f);
    glEnd();
    glPopMatrix();

    // Lit GLU sphere from a display list
    glPushMatrix();
    glTranslatef(1.2f, 0.0f, 0.0f);
    glRotatef(angle * 0.7f, 1.0f, 1.0f, 0.0f);
    glEnable(GL_LIGHTING);
    glColor3f(0.8f, 0.7f, 0.2f);
    glCallList(1);
    glPopMatrix();

    angle += frameTime * 60.0f;

#ifdef RS_XSCREENSAVER
    glXSwapBuffers(xdisplay, xwindow);
#endif
}

void idleProc()
{
    static rsTimer timer;
    frameTime = (float)timer.tick();
    if (readyToDraw && !isSuspended && !checkingPassword)
        draw();
}

void handleCommandLine(int argc, char* argv[])
{
    (void)argc; (void)argv;
}

void reshape(int width, int height)
{
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    aspectRatio = (float)width / (float)height;
    gluPerspective(60.0, aspectRatio, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW);
}

void initSaver()
{
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);

    // Display list with a GLU quadric sphere (exercises glues + gl4es lists)
    glNewList(1, GL_COMPILE);
        GLUquadricObj* qobj = gluNewQuadric();
        gluQuadricNormals(qobj, GLU_SMOOTH);
        gluSphere(qobj, 0.7f, 24, 18);
        gluDeleteQuadric(qobj);
    glEndList();

    glEnable(GL_LIGHT0);
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    float position[4] = {3.0f, 3.0f, 3.0f, 0.0f};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    const GLubyte* ver = glGetString(GL_VERSION);
    const GLubyte* ren = glGetString(GL_RENDERER);
    printf("testsaver: GL_VERSION  = %s\n", ver ? (const char*)ver : "(null)");
    printf("testsaver: GL_RENDERER = %s\n", ren ? (const char*)ren : "(null)");

    readyToDraw = 1;
}

void cleanUp()
{
    glDeleteLists(1, 1);
}
