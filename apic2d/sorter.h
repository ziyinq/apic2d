/*
 *  sorter.h
 *  flip3D
 */

#include "MathDefs.h"
#include <vector>
#ifndef _SORTER_H
#define _SORTER_H

struct Particle;
class FluidSim;

class sorter {
public:
	sorter( int ni_, int nj_ );
	~sorter();
	
	void sort( FluidSim* sim );
	void getNeigboringParticles_cell( int i, int j, int wl, int wh, int hl, int hh, std::vector<Particle *>& );
  
	int	 getNumParticleAt( int i, int j );
	void deleteAllParticles();
  
  std::vector< std::vector<Particle *> > cells;
  int ni;
  int nj;
};

#endif