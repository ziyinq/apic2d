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
#include <iomanip>
#include <sstream>
#include <string>

using namespace std;

//Try changing the grid resolution
int grid_resolution = 20;
scalar timestep = 0.01;
scalar grid_width = 2*M_PI;

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
void read_bgeo();


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

  sim.root_boundary = new FluidSim::Boundary(Vector2s(0, 0), Vector2s(2*M_PI, 2*M_PI), FluidSim::BT_BOX, true);

  sim.root_sources = NULL;

  sim.update_boundary();
//  sim.initDambreak(grid_resolution);
  sim.initTaylor(grid_resolution);

  Gluvi::run();

  delete sim.root_boundary;
//    read_bgeo();
  
  return 0;
}

//void read_bgeo()
//{
//    float m = 1.;
//    float G = 981.0;
//
//    ofstream myfile("apic_energy.txt");
//    std::string particleFile;
//    for (int i = 0; i <= 240; i++)
//    {
//        float kinetic_energy = 0;
//        float total_energy = 0;
//        particleFile = "../../output/apic_bgeo/frame_";
//        std::ostringstream ss;
//        ss << std::setw( 4 ) << std::setfill( '0' ) << i;
//        particleFile = particleFile + ss.str() + ".bgeo";
//        std::cout << particleFile << std::endl;
//        Partio::ParticlesData* data = Partio::read(particleFile.c_str());
//
//        Partio::ParticleAttribute posAttr, velAttr;
//        assert(data->attributeInfo("position", posAttr) && posAttr.type == Partio::VECTOR && posAttr.count == 3);
//        assert(data->attributeInfo("v", velAttr) && velAttr.type == Partio::VECTOR && velAttr.count == 3);
//
//        Partio::ParticleAccessor posAcc(posAttr);
//        Partio::ParticlesData::const_iterator it_p = data->begin();
//        it_p.addAccessor(posAcc);
//
//        for (; it_p != data->end(); ++it_p) {
//            float* pos = posAcc.raw<float>(it_p);
//            total_energy += m * G * pos[1];
//        }
//
//        Partio::ParticleAccessor velAcc(posAttr);
//        Partio::ParticlesData::const_iterator it_v = data->begin();
//        it_v.addAccessor(velAcc);
//        for (; it_v != data->end(); ++it_v) {
//            float* vel = velAcc.raw<float>(it_v);
//            Vector2s v(vel[0], vel[1]);
//            total_energy += 0.5f * m * (v[0]*v[0] + v[1]*v[1]);
//            kinetic_energy += 0.5f * m * (v[0]*v[0] + v[1]*v[1]);
//        }
//        data->release();
//
//        if (myfile.is_open())
//        {
//            myfile << kinetic_energy << " " << total_energy << "\n";
//            std::cout << kinetic_energy << " " << total_energy << std::endl;
//        }
//    }
//    myfile.close();
//}

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





