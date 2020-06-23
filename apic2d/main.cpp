#include <cstdio>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cfloat>

#include "gluvi.h"
#include "fluidsim.h"
#include "openglutils.h"
#include "array2_utils.h"

using namespace std;

//Try changing the grid resolution
int grid_resolution = 50;
scalar timestep = 0.005;
scalar grid_width = 100.0;

FluidSim sim;

//Gluvi stuff
//-------------
Gluvi::PanZoom2D cam(-0.1, -0.35, 1.2);
double oldmousetime;
Vector2s oldmouse;
void display();
void mouse(int button, int state, int x, int y);
void drag(int x, int y);
void timer(int junk);


//Boundary definition - several circles in a circular domain.

Vector2s c0(50,50), c1(70,50), c2(30,35), c3(50,70);
Vector2s s0(10,5);
scalar rad0 = 40,  rad1 = 10,  rad2 = 10,   rad3 = 10;
Vector2s o0(0.0, 0.0);

//Main testing code
//-------------
int main(int argc, char **argv)
{
  
  //Setup viewer stuff
  Gluvi::init("Basic Fluid Solver with Static Variational Boundaries", &argc, argv);
  Gluvi::camera=&cam;
  Gluvi::userDisplayFunc=display;
  Gluvi::userMouseFunc=mouse;
  Gluvi::userDragFunc=drag;
  glClearColor(1,1,1,1);
  
  glutTimerFunc(1000, timer, 0);
  
  //Set up the simulation
  sim.initialize(o0, grid_width, grid_resolution, grid_resolution, 1.0);

  sim.root_boundary = new FluidSim::Boundary(Vector2s(10, 10), Vector2s(80, 80), FluidSim::BT_BOX, true);
  
  sim.root_sources = NULL;

  sim.update_boundary();
  sim.initDambreak();

  Gluvi::run();
  
  delete sim.root_boundary;
  
  return 0;
}


void display(void)
{
  sim.render();
}

void mouse(int button, int state, int x, int y)
{
  Vector2s newmouse;
  cam.transform_mouse(x, y, newmouse.data());
  //double newmousetime=get_time_in_seconds();
  
  oldmouse=newmouse;
  //oldmousetime=newmousetime;
}

void drag(int x, int y)
{
  Vector2s newmouse;
  cam.transform_mouse(x, y, newmouse.data());
  //double newmousetime=get_time_in_seconds();
  
  oldmouse=newmouse;
  //oldmousetime=newmousetime;
}

void timer(int junk)
{
  sim.advance(timestep);
  
  glutPostRedisplay();
  glutTimerFunc(30, timer, 0);
  
}





