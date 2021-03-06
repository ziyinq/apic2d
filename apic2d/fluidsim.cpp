#include "fluidsim.h"
#include "sorter.h"
#include "kernel.h"

#include "array2_utils.h"

#include "pcgsolver/sparse_matrix.h"
#include "pcgsolver/pcg_solver.h"

#ifdef __APPLE__
#include <GLUT/glut.h> // why does Apple have to put glut.h here...
#else
#include <GL/glut.h> // ...when everyone else puts it here?
#include <fstream>

#endif

#include "openglutils.h"
#include "gluvi.h"

const scalar source_velocity = 40.0;
const int particle_correction_step = 1;

scalar fraction_inside(scalar phi_left, scalar phi_right);
void extrapolate(Array2s& grid, Array2s& old_grid, const Array2s& grid_weight, const Array2s& grid_liquid_weight, Array2c& valid, Array2c old_valid, const Vector2i& offset);

scalar FluidSim::cfl() {
  scalar maxvel = 0;
  for(int i = 0; i < u.a.size(); ++i)
    maxvel = fmax(maxvel, fabs(u.a[i]));
  for(int i = 0; i < v.a.size(); ++i)
    maxvel = fmax(maxvel, fabs(v.a[i]));
  return dx / maxvel;
}

FluidSim::~FluidSim()
{
  delete m_sorter;
}

void FluidSim::initialize(const Vector2s& origin_, scalar width, int ni_, int nj_, scalar rho_,
                          bool draw_grid_, bool draw_particles_, bool draw_velocities_, bool draw_boundaries_)
{
  rho = rho_;
  draw_grid = draw_grid_;
  draw_particles = draw_particles_;
  draw_velocities = draw_velocities_;
  draw_boundaries = draw_boundaries_;
  origin = origin_;
  ni = ni_;
  nj = nj_;
  dx = width / (scalar)ni;
  u.resize(ni+1,nj); temp_u.resize(ni+1,nj); u_weights.resize(ni+1,nj); u_valid.resize(ni+1,nj); saved_u.resize(ni+1, nj);
  v.resize(ni,nj+1); temp_v.resize(ni,nj+1); v_weights.resize(ni,nj+1); v_valid.resize(ni, nj+1); saved_v.resize(ni,nj+1);
  u_weight.resize(ni+1, nj); u_weight.set_zero();
  v_weight.resize(ni, nj+1); v_weight.set_zero();
  curl.resize(ni+1, nj+1);
  u.set_zero();
  v.set_zero();
  curl.set_zero();
  nodal_solid_phi.resize(ni+1,nj+1);
  valid.resize(ni+1, nj+1);
  old_valid.resize(ni+1, nj+1);
  liquid_phi.resize(ni,nj);
  m_sorter = new sorter(ni, nj);
}

//Initialize the grid-based signed distance field that dictates the position of the solid boundary
void FluidSim::update_boundary() {
  for(int j = 0; j < nj+1; ++j) for(int i = 0; i < ni+1; ++i) {
    Vector2s pos(i*dx,j*dx);
    nodal_solid_phi(i,j) = compute_phi(pos + origin);
  }
}

void FluidSim::resample(Vector2s& p, Vector2s& u, Matrix2s& c)
{
  std::vector<Particle*> neighbors;
  scalar wsum = 0.0;
  Vector2s save = u;
  Matrix2s csave = c;
  u = Vector2s::Zero();
  c = Matrix2s::Zero();
  
  int ix = std::max(0, std::min(ni - 1, (int)((p(0) - origin(0)) / dx)));
  int iy = std::max(0, std::min(nj - 1, (int)((p(1) - origin(1)) / dx)));
  
  m_sorter->getNeigboringParticles_cell(ix, iy, -1, 1, -1, 1, neighbors);
  
  const scalar re = dx;
  
  for(Particle* np : neighbors)
  {
    Vector2s diff = np->x - p;
    scalar w = 4.0 / 3.0 * M_PI * np->radii * np->radii * np->radii * rho * kernel::linear_kernel(diff, re);
    u += w * np->v;
    c += w * np->c;
    wsum += w;
  }
  
  if(wsum) {
    u /= wsum;
    c /= wsum;
  } else {
    u = save;
    c = csave;
  }
}

void FluidSim::compute_density()
{
  int np = (int) particles.size();
  
  const scalar re = dx;
  // Compute Pseudo Moved Point
  for(int n = 0; n < np; ++n)
  {
    Particle &p = particles[n];
    scalar dens = 0.0;
    
    int ix = std::max(0, std::min((int)((p.x(0) - origin(0)) / dx), ni));
    int iy = std::max(0, std::min((int)((p.x(1) - origin(1)) / dx), nj));
    
    std::vector<Particle*> neighbors;
    m_sorter->getNeigboringParticles_cell(ix, iy, -1, 1, -1, 1, neighbors);
    
    for(Particle* np : neighbors)
    {
      scalar dist = (p.x - np->x).norm();
      scalar w = kernel::poly6_kernel(dist * dist, re);
      dens += w;
    }
    
    p.dens = dens;
  }
}

void FluidSim::correct(scalar dt)
{
  int np = (int) particles.size();
  
  const scalar re = dx / sqrt(2.0) * 1.1;
  
  int offset = rand() % particle_correction_step;
  // Compute Pseudo Moved Point
  for(int n = 0; n < np; ++n)
  {
    if(n % particle_correction_step != offset) continue;
    
    Particle &p = particles[n];
    
    if(p.type != PT_LIQUID) continue;
    Vector2s spring = Vector2s::Zero();
    
    int ix = std::max(0, std::min((int)((p.x(0) - origin(0)) / dx), ni));
    int iy = std::max(0, std::min((int)((p.x(1) - origin(1)) / dx), nj));
    
    std::vector<Particle*> neighbors;
    m_sorter->getNeigboringParticles_cell(ix, iy, -1, 1, -1, 1, neighbors);
    
    for(Particle* np : neighbors)
    {
      if(&p != np)
      {
        scalar dist = (p.x - np->x).norm();
        scalar w = 50.0 * kernel::smooth_kernel(dist * dist, re);
        if( dist > 0.01 * re )
        {
          spring += w * (p.x - np->x) / dist * re;
        } else {
          spring(0) += 0.01 * re / dt * (rand() & 0xFF) / 255.0;
          spring(1) += 0.01 * re / dt * (rand() & 0xFF) / 255.0;
        }
      }
    }
    
    p.buf0 = p.x + dt * spring;
    
    Vector2s pp = (p.buf0 - origin)/dx;
    scalar phi_value = interpolate_value(pp, nodal_solid_phi);
    
    if(phi_value < 0) {
      Vector2s normal;
      interpolate_gradient(normal, pp, nodal_solid_phi);
      normal.normalize();
      p.buf0 -= phi_value*normal;
//      std::cout << normal << std::endl;
    }
  }
  
  // Update
  for(int n = 0; n < np; ++n)
  {
    if(n % particle_correction_step != offset) continue;
    Particle& p = particles[n];
    if(p.type != PT_LIQUID) continue;
    
    p.x = p.buf0;
  }
}

// The main fluid simulation step
void FluidSim::advance(scalar dt) {
  // Change here to try differnt integration scheme
  const INTEGRATOR_TYPE integration_scheme = IT_FLIP_BRIDSON;
  const scalar flip_coefficient = 1.0f;
  
  //Passively advect particles
  m_sorter->sort(this);
  
  map_p2g();

    if (integration_scheme == IT_FLIP_JIANG ||
      integration_scheme == IT_FLIP_BRACKBILL ||
      integration_scheme == IT_FLIP_BRIDSON) {
    save_velocity();
  }
  
//  add_force(dt);
  std::cout << "Before energy: " << gridEnergy() << std::endl;
  project(dt);
  std::cout << "After energy: " << gridEnergy() << std::endl;

  temp_u = u;
  temp_v = v;
  //Pressure projection only produces valid velocities in faces with non-zero associated face area.
  //Because the advection step may interpolate from these invalid faces,
  //we must extrapolate velocities from the fluid domain into these zero-area faces.
//  extrapolate(u, temp_u, u_weights, liquid_phi, valid, old_valid, Vector2i(-1, 0));
//  extrapolate(v, temp_v, v_weights, liquid_phi, valid, old_valid, Vector2i(0, -1));

    for (int j = 0; j <= 19; j++)
    {
        u(0,j) = 0;
        u(20,j) = 0;
    }
//        std::cout << u(0,j) << " " << u(20,j) << std::endl;

    for (int i = 10; i <= 19; i++){
        v(i,0) = 0;
        v(i, 20) = 0;
    }
//        std::cout << v(i,10) << " " << v(i, 30) << std::endl;

  //For extrapolated velocities, replace the normal component with
  //that of the object.
//  constrain_velocity();

  calculateCurl();
//  correct(dt);
  
  switch (integration_scheme) {
    case IT_PIC:
      map_g2p_pic(dt);
      break;
      
    case IT_FLIP_BRACKBILL:
      map_g2p_flip_brackbill(dt, flip_coefficient);
      break;
        
    case IT_FLIP_BRIDSON:
      map_g2p_flip_bridson(dt, flip_coefficient);
      break;
      
    case IT_FLIP_JIANG:
      map_g2p_flip_jiang(dt, flip_coefficient);
      break;
    
    case IT_APIC:
      map_g2p_apic(dt);
      break;

    case IT_DAPIC:
      map_g2p_dapic(dt);
      break;
      
    default:
      std::cerr << "Unknown integrator type!" << std::endl;
      break;
  }
    float vol_particle = 4*M_PI*M_PI / particles.size();
    float energy = 0;
    for (int i = 0; i < particles.size(); i++)
    {
        energy += vol_particle*particles[i].v.squaredNorm();
    }
    std::cout << "Frame " << count << ", " << energy << std::endl;

  particle_boundary_collision(dt);
  
}

float FluidSim::gridEnergy()
{
    float energy = 0;
    for(int j = 0; j < nj; ++j) for(int i = 0; i < ni+1; ++i) {
        energy += u_weight(i,j) * u(i,j) * u(i,j);
    }
    for(int j = 0; j < nj+1; ++j) for(int i = 0; i < ni; ++i) {
        energy += v_weight(i,j) * v(i,j) * v(i,j);
    }
    return energy;
}

void FluidSim::calculateCurl()
{
    curl.set_zero();
    for (int i = 0; i < ni; i++)
        for (int j = 0; j < nj; j++)
        {
            if(i>0 && i<ni && j>0 &&j<nj)
            {
                curl(i,j) = (u(i,j) - u(i,j-1) + v(i-1,j) - v(i,j))/dx;
            }
        }
}

void FluidSim::save_velocity()
{
  saved_u = u;
  saved_v = v;
}

void FluidSim::add_force(scalar dt) {
  // splat particles
  
  for(int j = 0; j < nj+1; ++j) for(int i = 0; i < ni; ++i) {
    v(i, j) += -981.0 * dt;
    Vector2s pos = Vector2s((i+0.5)*dx, j*dx) + origin;
    
    if(root_sources && compute_phi(pos, *root_sources) < dx) {
      v(i, j) = -source_velocity;
    }
  }
}

//For extrapolated points, replace the normal component
//of velocity with the object velocity (in this case zero).
void FluidSim::constrain_velocity() {
  temp_u = u;
  temp_v = v;
  
  //(At lower grid resolutions, the normal estimate from the signed
  //distance function is poor, so it doesn't work quite as well.
  //An exact normal would do better.)
  
  //constrain u
  for(int j = 0; j < u.nj; ++j) for(int i = 0; i < u.ni; ++i) {
    if(u_weights(i,j) == 0) {
      //apply constraint
      Vector2s pos = Vector2s(i*dx, (j+0.5)*dx) + origin;
      Vector2s vel = get_velocity(pos);
      Vector2s normal(0,0);
      interpolate_gradient(normal, Vector2s(i, j + 0.5), nodal_solid_phi);
      normal.normalize();
      scalar perp_component = vel.dot(normal);
      vel -= perp_component*normal;
      temp_u(i,j) = vel[0];
    }
  }
  
  //constrain v
  for(int j = 0; j < v.nj; ++j) for(int i = 0; i < v.ni; ++i) {
    if(v_weights(i,j) == 0) {
      //apply constraint
      Vector2s pos = Vector2s((i+0.5)*dx, j*dx) + origin;
      Vector2s vel = get_velocity(pos);
      Vector2s normal(0,0);
      interpolate_gradient(normal, Vector2s(i + 0.5, j), nodal_solid_phi);
      normal.normalize();
      scalar perp_component = vel.dot(normal);
      vel -= perp_component*normal;
      temp_v(i,j) = vel[1];
    }
  }
  
  //update
  u = temp_u;
  v = temp_v;
  
}

void FluidSim::compute_phi() {
  //Estimate from particles
  liquid_phi.assign(3*dx);
  for(int p = 0; p < particles.size(); ++p) {
    if(particles[p].type != PT_LIQUID) continue;
    
    const Vector2s& point = particles[p].x;
    int i,j;
    scalar fx,fy;
    //determine containing cell;
    get_barycentric((point[0])/dx-0.5, i, fx, 0, ni);
    get_barycentric((point[1])/dx-0.5, j, fy, 0, nj);
    
    //compute distance to surrounding few points, keep if it's the minimum
    for(int j_off = j-2; j_off<=j+2; ++j_off) for(int i_off = i-2; i_off<=i+2; ++i_off) {
      if(i_off < 0 || i_off >= ni || j_off < 0 || j_off >= nj)
        continue;
      
      Vector2s pos = Vector2s((i_off+0.5)*dx, (j_off+0.5)*dx) + origin;
      scalar phi_temp = (pos - point).norm() - std::max(particles[p].radii, dx * sqrt(2.0) / 2.0);
      liquid_phi(i_off,j_off) = min(liquid_phi(i_off,j_off), phi_temp);
    }
  }
  
  //"extrapolate" phi into solids if nearby
  for(int j = 0; j < nj; ++j) {
    for(int i = 0; i < ni; ++i) {
      Vector2s pos = Vector2s((i+0.5)*dx, (j+0.5)*dx) + origin;
      scalar solid_phi_val = compute_phi(pos);
      liquid_phi(i, j) = std::min(liquid_phi(i, j), solid_phi_val);
    }
  }
}

//Add a tracer particle for visualization
void FluidSim::add_particle(const Particle& p) {
  particles.push_back(p);
}

//move the particles in the fluid
void FluidSim::particle_boundary_collision(scalar dt) {

  for(int p = 0; p < particles.size(); ++p) {
    if(particles[p].type == PT_SOLID) continue;
    
    Vector2s pp = (particles[p].x - origin)/dx;
    
    //Particles can still occasionally leave the domain due to truncation errors,
    //interpolation error, or large timesteps, so we project them back in for good measure.
    
    //Try commenting this section out to see the degree of accumulated error.
    scalar phi_value = interpolate_value(pp, nodal_solid_phi);
    if(phi_value < 0) {
      Vector2s normal;
      interpolate_gradient(normal, pp, nodal_solid_phi);
      normal.normalize();
      particles[p].x -= phi_value*normal;
    }
  }
  
  m_sorter->sort(this);
  
  if(root_sources) {
    for(int j = 0; j < nj; ++j) for(int i = 0; i < ni; ++i)
    {
      int num_p_need = 2 - std::min(2, m_sorter->getNumParticleAt(i, j));
      for(int k = 0; k < num_p_need; ++k)
      {
        scalar x = ((scalar) i + 0.5 + (((scalar)rand() / (scalar)RAND_MAX) * 0.5 - 0.5) ) * dx;
        scalar y = ((scalar) j + 0.5 + (((scalar)rand() / (scalar)RAND_MAX) * 0.5 - 0.5) ) * dx;
        Vector2s pt = Vector2s(x, y) + origin;
        
        if(compute_phi(pt, *root_sources) < 0.0) {
          Vector2s pp = (pt - origin)/dx;
          scalar phi_value = interpolate_value(pp, nodal_solid_phi);
          if(phi_value > 0.0) {
            add_particle(Particle(pt, Vector2s(0.0, -source_velocity), dx / sqrt(2.0), PT_LIQUID));
          }
        }
      }
    }
  }
  
  particles.erase( std::remove_if(particles.begin(), particles.end(), [&] (const Particle& p) {
    return p.x(0) < origin(0) - 0.5 * dx || p.x(0) > origin(0) + ((scalar) ni+1.5) * dx || p.x(1) < origin(1) - 0.5 * dx || p.x(1) > origin(1) + ((scalar) nj+1.5) * dx;
  }), particles.end());
}

void FluidSim::project(scalar dt) {
  compute_phi();
  
  //Compute finite-volume type face area weight for each velocity sample.
  compute_weights();
  
  //Set up and solve the variational pressure solve.
  solve_pressure(dt);
  
}

//Interpolate velocity from the MAC grid.
Vector2s FluidSim::get_velocity(const Vector2s& position) {
  
  //Interpolate the velocity from the u and v grids
  Vector2s p = (position - origin) / dx;
  Vector2s p0 = p - Vector2s(0, 0.5);
  Vector2s p1 = p - Vector2s(0.5, 0);
  scalar u_value = interpolate_value(p0, u);
  scalar v_value = interpolate_value(p1, v);
  
  return Vector2s(u_value, v_value);
}

scalar FluidSim::get_vorticity(const Vector2s &position){

    Vector2s p = (position - origin) / dx;
    scalar curl_value = fabs(interpolate_value(p, curl)) / 10.;
    curl_value = fmin(fmax(curl_value, 0.0f), .99f);
    return curl_value;
}

Matrix2s FluidSim::get_affine_matrix(const Vector2s& position)
{
  Vector2s p = (position - origin) / dx;
  Vector2s p0 = p - Vector2s(0, 0.5);
  Vector2s p1 = p - Vector2s(0.5, 0);
  
  Matrix2s c;
  c.col(0) = affine_interpolate_value(p0, u) / dx;
  c.col(1) = affine_interpolate_value(p1, v) / dx;
  
  return c;
}

Matrix2s FluidSim::get_saved_affine_matrix(const Vector2s& position)
{
  Vector2s p = (position - origin) / dx;
  Vector2s p0 = p - Vector2s(0, 0.5);
  Vector2s p1 = p - Vector2s(0.5, 0);
  
  Matrix2s c;
  c.col(0) = affine_interpolate_value(p0, saved_u);
  c.col(1) = affine_interpolate_value(p1, saved_v);
  
  return c;
}

Vector2s FluidSim::get_saved_velocity(const Vector2s& position)
{
  //Interpolate the velocity from the u and v grids
  Vector2s p = (position - origin) / dx;
  Vector2s p0 = p - Vector2s(0, 0.5);
  Vector2s p1 = p - Vector2s(0.5, 0);
  scalar u_value = interpolate_value(p0, saved_u);
  scalar v_value = interpolate_value(p1, saved_v);
  
  return Vector2s(u_value, v_value);
}

//Given two signed distance values, determine what fraction of a connecting segment is "inside"
scalar fraction_inside(scalar phi_left, scalar phi_right) {
  if(phi_left < 0 && phi_right < 0)
    return 1;
  if (phi_left < 0 && phi_right >= 0)
    return phi_left / (phi_left - phi_right);
  if(phi_left >= 0 && phi_right < 0)
    return phi_right / (phi_right - phi_left);
  else
    return 0;
//    return 1;
}

//Compute finite-volume style face-weights for fluid from nodal signed distances
void FluidSim::compute_weights() {
  
  for(int j = 0; j < u_weights.nj; ++j) for(int i = 0; i < u_weights.ni; ++i) {
    u_weights(i,j) = 1 - fraction_inside(nodal_solid_phi(i,j+1), nodal_solid_phi(i,j));
    u_weights(i,j) = clamp(u_weights(i,j), 0.0, 1.0);
//      u_weights(i,j) = 1;
  }
  for(int j = 0; j < v_weights.nj; ++j) for(int i = 0; i < v_weights.ni; ++i) {
    v_weights(i,j) = 1 - fraction_inside(nodal_solid_phi(i+1,j), nodal_solid_phi(i,j));
    v_weights(i,j) = clamp(v_weights(i,j), 0.0, 1.0);
//    v_weights(i,j) = 1;
  }
  
}

//An implementation of the variational pressure projection solve for static geometry
void FluidSim::solve_pressure(scalar dt) {
  
  //This linear system could be simplified, but I've left it as is for clarity
  //and consistency with the standard naive discretization
  
  int ni = v.ni;
  int nj = u.nj;
  int system_size = ni*nj;
  if(rhs.size() != system_size) {
    rhs.resize(system_size);
    pressure.resize(system_size);
    matrix.resize(system_size);
  }
  matrix.zero();

    //Build the linear system for pressure
    for(int j = 1; j < nj-1; ++j) {
        for(int i = 1; i < ni-1; ++i) {
            int index = i + ni*j;
            rhs[index] = 0;
            pressure[index] = 0;

            //right neighbour
            float term = dt / sqr(dx);
            if(i + 1 < ni) {
                matrix.add_to_element(index, index, term);
                matrix.add_to_element(index, index + 1, -term);
            }
            else {
//                float theta = fraction_inside(centre_phi, right_phi);
//                if(theta < 0.01) theta = 0.01;
//                matrix.add_to_element(index, index, term/theta);
            }
            rhs[index] -= u(i+1,j) / dx;

            //left neighbour
            term = dt / sqr(dx);
            if(i - 1 >= 0) {
                matrix.add_to_element(index, index, term);
                matrix.add_to_element(index, index - 1, -term);
            }
            else {
//                float theta = fraction_inside(centre_phi, left_phi);
//                if(theta < 0.01) theta = 0.01;
//                matrix.add_to_element(index, index, term/theta);
            }
            rhs[index] += u(i,j) / dx;

            //top neighbour
            term = dt / sqr(dx);
            if(j + 1 < nj) {
                matrix.add_to_element(index, index, term);
                matrix.add_to_element(index, index + ni, -term);
            }
            else {
//                float theta = fraction_inside(centre_phi, top_phi);
//                if(theta < 0.01) theta = 0.01;
//                matrix.add_to_element(index, index, term/theta);
            }
            rhs[index] -= v(i,j+1) / dx;

            //bottom neighbour
            term = dt / sqr(dx);
            if(j - 1 >= 0) {
                matrix.add_to_element(index, index, term);
                matrix.add_to_element(index, index - ni, -term);
            }
            else {
//                float theta = fraction_inside(centre_phi, bot_phi);
//                if(theta < 0.01) theta = 0.01;
//                matrix.add_to_element(index, index, term/theta);
            }
            rhs[index] += v(i,j) / dx;
        }
    }
//  //Build the linear system for pressure
//  for(int j = 1; j < nj-1; ++j) {
//    for(int i = 1; i < ni-1; ++i) {
//      int index = i + ni*j;
//      rhs[index] = 0;
//      pressure[index] = 0;
//      float centre_phi = liquid_phi(i,j);
//      if(centre_phi < 0 && (u_weights(i,j) > 0.0 || u_weights(i+1,j) > 0.0 || v_weights(i,j) > 0.0 || v_weights(i,j+1) > 0.0)) {
//
//        //right neighbour
//        float term = u_weights(i+1,j) * dt / sqr(dx);
//        float right_phi = liquid_phi(i+1,j);
//        if(right_phi < 0) {
//          matrix.add_to_element(index, index, term);
//          matrix.add_to_element(index, index + 1, -term);
//        }
//        else {
//          float theta = fraction_inside(centre_phi, right_phi);
//          if(theta < 0.01) theta = 0.01;
//          matrix.add_to_element(index, index, term/theta);
//        }
//        rhs[index] -= u_weights(i+1,j)*u(i+1,j) / dx;
//
//        //left neighbour
//        term = u_weights(i,j) * dt / sqr(dx);
//        float left_phi = liquid_phi(i-1,j);
//        if(left_phi < 0) {
//          matrix.add_to_element(index, index, term);
//          matrix.add_to_element(index, index - 1, -term);
//        }
//        else {
//          float theta = fraction_inside(centre_phi, left_phi);
//          if(theta < 0.01) theta = 0.01;
//          matrix.add_to_element(index, index, term/theta);
//        }
//        rhs[index] += u_weights(i,j)*u(i,j) / dx;
//
//        //top neighbour
//        term = v_weights(i,j+1) * dt / sqr(dx);
//        float top_phi = liquid_phi(i,j+1);
//        if(top_phi < 0) {
//          matrix.add_to_element(index, index, term);
//          matrix.add_to_element(index, index + ni, -term);
//        }
//        else {
//          float theta = fraction_inside(centre_phi, top_phi);
//          if(theta < 0.01) theta = 0.01;
//          matrix.add_to_element(index, index, term/theta);
//        }
//        rhs[index] -= v_weights(i,j+1)*v(i,j+1) / dx;
//
//        //bottom neighbour
//        term = v_weights(i,j) * dt / sqr(dx);
//        float bot_phi = liquid_phi(i,j-1);
//        if(bot_phi < 0) {
//          matrix.add_to_element(index, index, term);
//          matrix.add_to_element(index, index - ni, -term);
//        }
//        else {
//          float theta = fraction_inside(centre_phi, bot_phi);
//          if(theta < 0.01) theta = 0.01;
//          matrix.add_to_element(index, index, term/theta);
//        }
//        rhs[index] += v_weights(i,j)*v(i,j) / dx;
//      }
//    }
//  }

    //Solve the system using Robert Bridson's incomplete Cholesky PCG solver
  
  scalar tolerance;
  int iterations;
  bool success = solver.solve(matrix, rhs, pressure, tolerance, iterations);
  if(!success) {
    printf("WARNING: Pressure solve failed!************************************************\n");
  }

//  for (int i = 0; i < pressure.size(); i++)
//      std::cout << pressure[i] << std::endl;
//  getchar();
  
  //Apply the velocity update
  u_valid.assign(0);
  for(int j = 0; j < u.nj; ++j) for(int i = 1; i < u.ni-1; ++i) {
    int index = i + j*ni;
    if(u_weights(i,j) > 0) {
      if(liquid_phi(i,j) < 0 || liquid_phi(i-1,j) < 0) {
        float theta = 1;
        if(liquid_phi(i,j) >= 0 || liquid_phi(i-1,j) >= 0)
          theta = fraction_inside(liquid_phi(i-1,j), liquid_phi(i,j));
        if(theta < 0.01) theta = 0.01;
        u(i,j) -= dt  * (pressure[index] - pressure[index-1]) / dx / theta;
        u_valid(i,j) = 1;
      }
    }
    else
      u(i,j) = 0;
  }
  v_valid.assign(0);
  for(int j = 1; j < v.nj-1; ++j) for(int i = 0; i < v.ni; ++i) {
    int index = i + j*ni;
    if(v_weights(i,j) > 0) {
      if (liquid_phi(i,j) < 0 || liquid_phi(i,j-1) < 0) {
        float theta = 1;
        if(liquid_phi(i,j) >= 0 || liquid_phi(i,j-1) >= 0)
          theta = fraction_inside(liquid_phi(i,j-1), liquid_phi(i,j));
        if(theta < 0.01) theta = 0.01;
        v(i,j) -= dt  * (pressure[index] - pressure[index-ni]) / dx / theta;
        v_valid(i,j) = 1;
      }
    }
    else
      v(i,j) = 0;
  }


    //Build the linear system for pressure
    for(int j = 1; j < nj-1; ++j) {
        for(int i = 1; i < ni-1; ++i) {
            int index = i + ni*j;
            rhs[index] = 0;
            pressure[index] = 0;
            float centre_phi = liquid_phi(i,j);
            if(centre_phi < 0 && (u_weights(i,j) > 0.0 || u_weights(i+1,j) > 0.0 || v_weights(i,j) > 0.0 || v_weights(i,j+1) > 0.0)) {
                rhs[index] -= u_weights(i+1,j)*u(i+1,j) / dx;
                rhs[index] += u_weights(i,j)*u(i,j) / dx;
                rhs[index] -= v_weights(i,j+1)*v(i,j+1) / dx;
                rhs[index] += v_weights(i,j)*v(i,j) / dx;
            }
        }
    }
}

scalar FluidSim::compute_phi(const Vector2s& pos, const Boundary& b) const
{
  switch (b.type) {
    case BT_BOX:
      return b.sign * box_phi(pos, b.center, b.parameter);
    case BT_CIRCLE:
      return b.sign * circle_phi(pos, b.center, b.parameter(0));
    case BT_TORUS:
      return b.sign * torus_phi(pos, b.center, b.parameter(0), b.parameter(1));
    case BT_TRIANGLE:
      return b.sign * triangle_phi(pos, b.center, b.parameter(0));
    case BT_HEXAGON:
      return b.sign * hexagon_phi(pos, b.center, b.parameter(0));
    case BT_CYLINDER:
      return b.sign * cylinder_phi(pos, b.center, b.parameter(0), b.parameter(1));
    case BT_UNION:
      return union_phi(compute_phi(pos, *b.op0), compute_phi(pos, *b.op1));
    case BT_INTERSECTION:
      return intersection_phi(compute_phi(pos, *b.op0), compute_phi(pos, *b.op1));
    default:
      return 1e+20;
  }
}

scalar FluidSim::compute_phi(const Vector2s& pos) const
{
  return compute_phi(pos, *root_boundary);
}

void FluidSim::init_random_particles()
{
  int num_particle = ni * nj;
  for(int i = 0; i < ni; ++i)
  {
    for(int j = 0; j < nj; ++j) {
      for(int k = 0; k < 2; ++k) {
        scalar x = ((scalar) i + 0.5 + (((scalar)rand() / (scalar)RAND_MAX) * 2.0 - 1.0) ) * dx;
        scalar y = ((scalar) j + 0.5 + (((scalar)rand() / (scalar)RAND_MAX) * 2.0 - 1.0) ) * dx;
        Vector2s pt = Vector2s(x,y) + origin;
        
        scalar phi = compute_phi(pt);
        if(phi > dx * 20.0) add_particle(Particle(pt, Vector2s::Zero(), dx / sqrt(2.0), PT_LIQUID));
      }
    }
  }
}

void FluidSim::initDambreak(int grid_resolution)
{
    for(int i = 0.1*grid_resolution; i < 0.4*grid_resolution; ++i)
    {
        for(int j = 0.1*grid_resolution; j < 0.9*grid_resolution; ++j) {
            for(int k = 0; k < 4; ++k) {
                scalar x = ((scalar) i + 0.5 + (((scalar)rand() / (scalar)RAND_MAX) * 2.0 - 1.0) ) * dx;
                scalar y = ((scalar) j + 0.5 + (((scalar)rand() / (scalar)RAND_MAX) * 2.0 - 1.0) ) * dx;
                Vector2s pt = Vector2s(x,y) + origin;
                add_particle(Particle(pt, Vector2s::Zero(), dx / sqrt(2.0), PT_LIQUID));
            }
        }
    }
}

void FluidSim::initTaylor(int grid_resolution)
{
    for(int i = 0; i < grid_resolution; i++)
    {
        for(int j = 0; j < grid_resolution; j++) {
            for (int k = 0; k < 2; k++)
                for (int l = 0; l < 2; l++)
                {
                    scalar x = ((scalar) i + 0.5*k + 0.25) * dx;
                    scalar y = ((scalar) j + 0.5*l + 0.25) * dx;
                    Vector2s pt = Vector2s(x,y) + origin;
                    Vector2s pos = (pt - Vector2s(2*M_PI, 2*M_PI));
                    add_particle(Particle(pt, Vector2s(-sin(pos.x())*cos(pos.y()), cos(pos.x())*sin(pos.y())), dx / sqrt(2.0), PT_LIQUID));
                }
        }
    }
    float vol_particle = 4*M_PI*M_PI / particles.size();
    float energy = 0;
    for (int i = 0; i < particles.size(); i++)
    {
        energy += vol_particle*particles[i].v.squaredNorm();
    }
    std::cout << energy << std::endl;
}

void FluidSim::map_p2g()
{
    float vol_particle = 4*M_PI*M_PI / particles.size();

    //u-component of velocity
  for(int j = 0; j < nj; ++j) for(int i = 0; i < ni+1; ++i) {
    Vector2s pos = Vector2s(i*dx, (j+0.5)*dx) + origin;
    std::vector<Particle *> neighbors;
    m_sorter->getNeigboringParticles_cell(i, j, -1, 0, -1, 1, neighbors);
    
    scalar sumw = 0.0;
    scalar sumu = 0.0;
    for(Particle* p : neighbors)
    {
//      scalar w = 4.0 / 3.0 * M_PI * rho * p->radii * p->radii * p->radii * kernel::linear_kernel(p->x - pos, dx);
      scalar w = vol_particle * kernel::linear_kernel(p->x - pos, dx);
      sumu += w * (p->v(0) + p->c.col(0).dot(pos - p->x));
      sumw += w;
    }
    
    u(i, j) = sumw ? sumu / sumw : 0.0;
    u_weight(i,j) = sumw;
  }
  
  //v-component of velocity
  for(int j = 0; j < nj+1; ++j) for(int i = 0; i < ni; ++i) {
    Vector2s pos = Vector2s((i+0.5)*dx, j*dx) + origin;
    std::vector<Particle *> neighbors;
    m_sorter->getNeigboringParticles_cell(i, j, -1, 1, -1, 0, neighbors);
    
    scalar sumw = 0.0;
    scalar sumu = 0.0;
    for(Particle* p : neighbors)
    {
//      scalar w = 4.0 / 3.0 * M_PI * rho * p->radii * p->radii * p->radii * kernel::linear_kernel(p->x - pos, dx);
      scalar w = vol_particle* kernel::linear_kernel(p->x - pos, dx);
      sumu += w * (p->v(1) + p->c.col(1).dot(pos - p->x));
      sumw += w;
    }
    
    v(i, j) = sumw ? sumu / sumw : 0.0;
    v_weight(i,j) = sumw;
  }
}

/*!
 \brief  PIC scheme
 */
void FluidSim::map_g2p_pic(float dt)
{
  for(Particle& p : particles)
  {
    if(p.type == PT_SOLID) continue;
    
    p.v = get_velocity(p.x);
    p.x += p.v * dt;
  }
}

/*!
 \brief  APIC scheme
 */
void FluidSim::map_g2p_apic(float dt)
{
  for(Particle& p : particles)
  {
    if(p.type == PT_SOLID) continue;
    
    p.v = get_velocity(p.x);
    p.c = get_affine_matrix(p.x);

//      double c11 = p.v[0];
//      double c12 = p.c(0,0);
//      double c13 = p.c(1,0);
//      double c21 = p.v[1];
//      double c22 = p.c(0,1);
//      double c23 = p.c(1,1);
//      double x0 = p.x[0];
//      double y0 = p.x[1];
//
//      double n = c13*c21 - c11*c23;
//      double yn = c11*c22 - c12*c21;
//      double m = sqrt(4*c13*c22+sqr(c12-c23));
//      double o = c12 + c23;
//      double d = c13*c22-c12*c23;
//
//      double test_x = (2*(-c13*c21+c11*c23+c13*c22*x0-c12*c23*x0)
//                       + 2*exp(0.5*(c12+c23)*dt)*((c13*c21-c11*c23)*cosh(0.5*m*dt)-
//                                                  (c12*c13*c21-2*c11*c13*c22+c11*c12*c23+c13*c21*c23-c11*c23*c23)*sinh(0.5*m*dt)/m))
//                      / (2*(c13*c22-c12*c23));
//      double test_y = (2*(-c11*c22+c13*c22*y0+c12*(c21-c23*y0))
//                       + 2*exp(0.5*(c12+c23)*dt)*((-c12*c21+c11*c22)*cosh(0.5*m*dt)+
//                                                  (c12*c12*c21-c11*c12*c22+2*c13*c21*c22-(c12*c21+c11*c22)*c23)*sinh(0.5*m*dt)/m))
//                      / (2*(c13*c22-c12*c23));
//      Vector2s pos = p.x + p.v*dt;
//
//      if (m != m)
//      {
//          if (abs(d) < 5e-4)
//          {
//              m = abs(c12+c23);
//          }
//          else
//          {
//              p.x[0] = x0 + (-n + exp(0.5*o*dt)*(n*cos(0.5*dt)+(2*d*c11-n*o)*sin(0.5*dt))) / d;
//              p.x[1] = y0 + (-yn + exp(0.5*o*dt)*(yn*cos(0.5*dt)+(2*d*c21-yn*o)*sin(0.5*dt))) / d;
//          }
//      }
//      if (m < 1e-6) {
//            p.x[0] = x0 + 0.5*n*dt*(exp(0.5*o*dt) - exp(-0.5*o*dt)) +
//                    0.5*exp(0.5*o*dt)*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c11*dt;
//
//            p.x[1] = y0 + 0.5*yn*dt*(exp(0.5*o*dt) - exp(-0.5*o*dt)) +
//                    0.5*exp(0.5*o*dt)*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c21*dt;
//            assert(m==m);
//      }
//      if (m >= 1e-6)
//      {
//            p.x[0] = x0 + n*dt/m*(robust_expm1(0.5*(m+o)*dt) - robust_expm1(0.5*(o-m)*dt)) +
//                    0.5*exp(0.5*o*dt)*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c11*dt;
//
//            p.x[1] = y0 + yn*dt/m*(robust_expm1(0.5*(m+o)*dt) - robust_expm1(0.5*(o-m)*dt)) +
//                    0.5*exp(0.5*o*dt)*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c21*dt;
//            assert(m==m);
//      }
//      Vector2s pos_diff = p.x - pos;
//      if (pos_diff.squaredNorm() > 1e-5)
//          std::cout << "n: " << n << ", yn: " << yn << ", m: " << m << ", d:" << d << std::endl;

        p.x += p.v * dt;
        p.vort = get_vorticity(p.x);
  }
}

double FluidSim::robust_expm1(double x)
{
    if (abs(x) < 1e-8)
        return 1;
    else
        return std::expm1(x)/x;
}

void FluidSim::map_g2p_dapic(float dt)
{
    Vector3sT k1 = Vector3sT(0,1,0);
    Vector3sT k2 = Vector3sT(0,0,1);
    for(Particle& p : particles)
    {
        if(p.type == PT_SOLID) continue;

        p.v = get_velocity(p.x);
        p.c = construct_dapic_c(p.x);
        double c11 = p.v[0];
        double c12 = p.c(0,0);
        double c13 = p.c(1,0);
        double c21 = p.v[1];
        double c22 = p.c(0,1);
        double c23 = p.c(1,1);
        double x0 = p.x[0];
        double y0 = p.x[1];

        double n = c13*c21 - c11*c23;
        double yn = c11*c22 - c12*c21;
        double m = sqrt(4*c13*c22+sqr(c12-c23));
        double d = c13*c22-c12*c23;
        double o = c12 + c23;
        o = 0;

        double test_x = (2*(-c13*c21+c11*c23+c13*c22*x0-c12*c23*x0)
                         + 2*exp(0.5*(c12+c23)*dt)*((c13*c21-c11*c23)*cosh(0.5*m*dt)-
                                                    (c12*c13*c21-2*c11*c13*c22+c11*c12*c23+c13*c21*c23-c11*c23*c23)*sinh(0.5*m*dt)/m))
                        / (2*(c13*c22-c12*c23));
        double test_y = (2*(-c11*c22+c13*c22*y0+c12*(c21-c23*y0))
                         + 2*exp(0.5*(c12+c23)*dt)*((-c12*c21+c11*c22)*cosh(0.5*m*dt)+
                                                    (c12*c12*c21-c11*c12*c22+2*c13*c21*c22-(c12*c21+c11*c22)*c23)*sinh(0.5*m*dt)/m))
                        / (2*(c13*c22-c12*c23));
        Vector2s pos = p.x + p.v*dt;
        if (m != m)
        {
            if (abs(d) < 5e-4)
            {
                m = 0;
            }
            else
            {
                p.x[0] = x0 + (-n + (n*cos(0.5*dt)+2*d*c11*sin(0.5*dt))) / d;
                p.x[1] = y0 + (-yn + (yn*cos(0.5*dt)+2*d*c21*sin(0.5*dt))) / d;
            }
        }
        if (m < 1e-6) {
            p.x[0] = x0 + 0.5*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c11*dt;

            p.x[1] = y0 + 0.5*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c21*dt;
        }
        if (m >= 1e-6)
        {
            p.x[0] = x0 + n*dt/m*(robust_expm1(0.5*m*dt) - robust_expm1(0.5*-m*dt)) +
                     0.5*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c11*dt;

            p.x[1] = y0 + yn*dt/m*(robust_expm1(0.5*m*dt) - robust_expm1(0.5*-m*dt)) +
                     0.5*(robust_expm1(0.5*m*dt) + robust_expm1(-0.5*m*dt))*c21*dt;
        }
        Vector2s pos_diff = p.x - pos;
        if (pos_diff.squaredNorm() > 1e-5)
            std::cout << "n: " << n << ", yn: " << yn << ", m: " << m << ", d:" << d << std::endl;

        p.vort = get_vorticity(p.x);
    }
}

Matrix2s FluidSim::construct_dapic_c(Vector2s &position)
{
    Matrix7s A = Matrix7s::Zero();
    Matrix3s Mu = Matrix3s::Zero();
    Matrix3s Mv = Matrix3s::Zero();
    Vector7s b = Vector7sT::Zero();
    Vector7s x;
    Vector6s k;
    k << 0 , 1, 0, 0, 0, 1;

    Vector2s p = (position - origin) / dx;
    Vector2s p0 = p - Vector2s(0, 0.5);
    Vector2s p1 = p - Vector2s(0.5, 0);

    Vector3s bu, bv;
    interpolate_M(p0, u, dx, Mu, bu);
    interpolate_M(p1, v, dx, Mv, bv);
    A.block<3,3>(0,0) = Mu;
    A.block<3,3>(3,3) = Mv;
    A.block<1,6>(6,0) = k.transpose();
    A.block<6,1>(0,6) = k;

    b.block<3,1>(0,0) = bu;
    b.block<3,1>(3,0) = bv;
    x = A.fullPivLu().solve(b);
    Matrix2s c;
    c.col(0) = x.block<2,1>(1,0);
    c.col(1) = x.block<2,1>(4,0);
    return c;
}

/*!
 \brief  FLIP scheme used in the 1986 paper from Brackbill
 */
void FluidSim::map_g2p_flip_brackbill(float dt, const scalar coeff)
{
  for(Particle& p : particles)
  {
    if(p.type == PT_SOLID) continue;
    
    Vector2s next_grid_velocity = get_velocity(p.x);
    Vector2s original_grid_velocity = get_saved_velocity(p.x);
    Vector2s diff_grid_velocity = next_grid_velocity - original_grid_velocity;
    
    p.v += diff_grid_velocity;
    p.x += (original_grid_velocity + diff_grid_velocity * coeff) * dt;
  }
}

/*!
 \brief  FLIP scheme from Bridson
 */
void FluidSim::map_g2p_flip_bridson(float dt, const scalar coeff)
{
  for(Particle& p : particles)
  {
    if(p.type == PT_SOLID) continue;
    
    Vector2s next_grid_velocity = get_velocity(p.x);
    Vector2s original_grid_velocity = get_saved_velocity(p.x);
    Vector2s diff_grid_velocity = next_grid_velocity - original_grid_velocity;
    
    p.v = next_grid_velocity + (p.v + diff_grid_velocity - next_grid_velocity) * coeff;
//    p.x += p.v * dt;
    p.x += next_grid_velocity * dt;
  }
}

/*!
 \brief  FLIP scheme used in the APIC paper from Jiang et al.
 */
void FluidSim::map_g2p_flip_jiang(float dt, const scalar coeff)
{
  for(Particle& p : particles)
  {
    if(p.type == PT_SOLID) continue;
    
    Vector2s next_grid_velocity = get_velocity(p.x);
    Vector2s original_grid_velocity = get_saved_velocity(p.x);
    Vector2s diff_grid_velocity = next_grid_velocity - original_grid_velocity;
    
    p.v = next_grid_velocity + (p.v + diff_grid_velocity - next_grid_velocity) * coeff;
    p.x += next_grid_velocity * dt;
  }
}

void FluidSim::render_boundaries(const Boundary& b)
{
  switch (b.type) {
    case BT_CIRCLE:
      draw_circle2d(b.center, b.parameter(0), 64);
      break;
    case BT_BOX:
      draw_box2d(b.center, b.parameter(0), b.parameter(1));
      break;
    case BT_TORUS:
      draw_circle2d(b.center, b.parameter(0), 64);
      draw_circle2d(b.center, b.parameter(1), 64);
      break;
    case BT_HEXAGON:
      draw_circle2d(b.center, b.parameter(0), 6);
      break;
    case BT_TRIANGLE:
      draw_circle2d(b.center, b.parameter(0), 3);
      break;
    case BT_UNION: case BT_INTERSECTION:
      render_boundaries(*b.op0);
      render_boundaries(*b.op1);
      break;
    default:
      break;
  }
}

void FluidSim::render()
{
  glPushMatrix();
  glScaled(1.0 / (dx * ni), 1.0 / (dx * ni), 1.0 / (dx * ni));
  
  if(draw_grid) {
    glColor3f(0.75,0.75,0.75);
    glLineWidth(1);
    draw_grid2d(origin, dx, ni, nj);
  }
  
  if(draw_boundaries) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    render_boundaries(*root_boundary);
  }
  
  if(draw_particles) {
    glColor3d(0,0,1);
    glPointSize(5);
    glBegin(GL_POINTS);
    for(unsigned int i = 0; i < particles.size(); ++i) {
      if(particles[i].type == PT_LIQUID)
        glVertex2dv(particles[i].x.data());
    }
    glEnd();
    
    glColor3d(1,0,0);
    glBegin(GL_POINTS);
    for(unsigned int i = 0; i < particles.size(); ++i) {
      if(particles[i].type == PT_SOLID)
        glVertex2dv(particles[i].x.data());
    }
    glEnd();
  }
  if(draw_velocities) {
    glColor3d(1,0,0);
    for(int j = 0;j < nj; ++j) for(int i = 0; i < ni; ++i) {
      Vector2s pos = Vector2s((i+0.5)*dx,(j+0.5)*dx) + origin;
      draw_arrow2d(pos, pos + 0.01 * get_velocity(pos), 0.1 * dx);
    }
  }
  
  glPopMatrix();
  if (count % 10 == 0)
  {
      save_bgeo();
  }
//  std::string filename = "../../output/image_" + std::to_string(count) + ".ppm";
//  Gluvi::ppm_screenshot(filename.c_str());
  count += 1;
}

FluidSim::Boundary::Boundary(const Vector2s& center_, const Vector2s& parameter_, BOUNDARY_TYPE type_, bool inside)
: center(center_), parameter(parameter_), type(type_), sign(inside ? -1.0 : 1.0)
{}

FluidSim::Boundary::Boundary(Boundary* op0_, Boundary* op1_, BOUNDARY_TYPE type_)
: op0(op0_), op1(op1_), type(type_), sign(op0_ ? op0_->sign : false)
{}

Particle::Particle(const Vector2s& x_, const Vector2s& v_, const scalar& radii_, ParticleType type_)
: x(x_), v(v_), radii(radii_), dens(0), type(type_)
{
  c.setZero();
  buf0.setZero();
}

Particle::Particle()
: x(Vector2s::Zero()), v(Vector2s::Zero()), radii(0.0), dens(0), type(PT_LIQUID), vort(0)
{
  c.setZero();
  buf0.setZero();
}

Particle::Particle(const Particle& p)
: x(p.x), v(p.v), radii(p.radii), dens(0), type(p.type), vort(p.vort)
{
  c.setZero();
  buf0.setZero();
}

//Apply several iterations of a very simple "Jacobi"-style propagation of valid velocity data in all directions
void extrapolate(Array2s& grid, Array2s& old_grid, const Array2s& grid_weight, const Array2s& grid_liquid_weight, Array2c& valid, Array2c old_valid, const Vector2i& offset) {
  
  //Initialize the list of valid cells
  for(int j = 0; j < valid.nj; ++j) valid(0,j) = valid(valid.ni-1,j) = 0;
  for(int i = 0; i < valid.ni; ++i) valid(i,0) = valid(i,valid.nj-1) = 0;
  
  Array2s* pgrids[] = {&grid, &old_grid};
  Array2c* pvalids[] = {&valid, &old_valid};
  
  for(int j = 1; j < grid.nj - 1; ++j) for(int i = 1; i < grid.ni - 1; ++i)
    valid(i,j) = grid_weight(i,j) > 0 && (grid_liquid_weight(i, j) < 0 || grid_liquid_weight(i + offset(0), j + offset(1)) < 0);
  
  old_valid = valid;
  
  for(int layers = 0; layers < 1; ++layers) {
    
    Array2s* pgrid_source = pgrids[layers & 1];
    Array2s* pgrid_target = pgrids[!(layers & 1)];
    
    Array2c* pvalid_source = pvalids[layers & 1];
    Array2c* pvalid_target = pvalids[!(layers & 1)];
    
    for(int j = 1; j < grid.nj-1; ++j) for(int i = 1; i < grid.ni-1; ++i) {
      scalar sum = 0;
      int count = 0;
      
      if(!(*pvalid_source)(i,j)) {
        
        if((*pvalid_source)(i+1,j)) {
          sum += (*pgrid_source)(i+1,j);
          ++count;
        }
        if((*pvalid_source)(i-1,j)) {
          sum += (*pgrid_source)(i-1,j);
          ++count;
        }
        if((*pvalid_source)(i,j+1)) {
          sum += (*pgrid_source)(i,j+1);
          ++count;
        }
        if((*pvalid_source)(i,j-1)) {
          sum += (*pgrid_source)(i,j-1);
          ++count;
        }
        
        //If any of neighbour cells were valid,
        //assign the cell their average value and tag it as valid
        if(count > 0) {
          (*pgrid_target)(i,j) = sum /(scalar)count;
          (*pvalid_target)(i,j) = 1;
        }
      }
    }
    
    *pgrid_source = *pgrid_target;
    *pvalid_source = *pvalid_target;
  }
}

void FluidSim::save_bgeo()
{
    std::string filestr = std::string("../../output/frame_") + std::string("\%04d.bgeo");
    char filename[1024];
    int framenum = count / 10;
    sprintf(filename, filestr.c_str(), framenum);
    Partio::ParticlesDataMutable* parts = Partio::create();
    Partio::ParticleAttribute vH, posH, vortH;
    vH = parts->addAttribute("v", Partio::VECTOR, 3);
    posH = parts->addAttribute("position", Partio::VECTOR, 3);
    vortH = parts->addAttribute("vorticity", Partio::FLOAT, 1);
    for (int i=0; i< particles.size(); i++){
        if(particles[i].type == PT_LIQUID)
        {
            int idx = parts->addParticle();
            float* _p = parts->dataWrite<float>(posH, idx);
            float* _v = parts->dataWrite<float>(vH, idx);
            float* _vort = parts->dataWrite<float>(vortH, idx);
            _p[0] = particles[i].x[0];
            _p[1] = particles[i].x[1];
            _p[2] = 0;
            _v[0] = particles[i].v[0];
            _v[1] = particles[i].v[1];
            _v[2] = 0;
            _vort[0] = particles[i].vort;
        }
    }
    Partio::write(filename, *parts);
    parts->release();
}


