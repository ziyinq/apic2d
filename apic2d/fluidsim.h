#ifndef FLUIDSIM_H
#define FLUIDSIM_H

#include "MathDefs.h"
#include "array2.h"
#include "pcgsolver/pcg_solver.h"
#include <Partio.h>
#include <vector>


class sorter;

enum ParticleType
{
  PT_LIQUID,
  PT_SOLID
};

struct Particle
{
  Particle(const Vector2s& x_, const Vector2s& v_, const scalar& radii_, ParticleType type);
  Particle();
  Particle(const Particle&);
  
  Vector2s x;
  Vector2s v;
  Matrix2s c;
  
  Vector2s buf0;

  scalar vort;
  scalar radii;
  scalar dens;
  ParticleType type;
};

class FluidSim {
public:
  virtual ~FluidSim();
  
  scalar rho;
  int count = 0;
  
  enum INTEGRATOR_TYPE
  {
    IT_PIC,
    IT_FLIP_BRACKBILL,
    IT_FLIP_BRIDSON,
    IT_FLIP_JIANG,
    IT_APIC,
    IT_DAPIC,
    
    IT_COUNT
  };
  
  enum BOUNDARY_TYPE
  {
    BT_CIRCLE,
    BT_BOX,
    BT_HEXAGON,
    BT_TRIANGLE,
    BT_TORUS,
    BT_CYLINDER,
    
    BT_INTERSECTION,
    BT_UNION,
    
    BT_COUNT
  };
  
  struct Boundary
  {
    Boundary(const Vector2s& center_, const Vector2s& parameter_, BOUNDARY_TYPE type_, bool inside);
    
    Boundary(Boundary* op0_, Boundary* op1_, BOUNDARY_TYPE type_);
    
    Vector2s center;
    Vector2s parameter;
    
    Boundary* op0;
    Boundary* op1;
    
    BOUNDARY_TYPE type;
    scalar sign;
  };
  
  void initialize(const Vector2s& origin_, scalar width, int ni_, int nj_, scalar rho_,
                  bool draw_grid_ = true, bool draw_particles_ = true, bool draw_velocities_ = true, bool draw_boundaries_ = true);
  void advance(scalar dt);
  void update_boundary();
  void init_random_particles();
  void initDambreak(int grid_resolution);
  void initTaylor(int grid_resolution);
  void render();
  void render_boundaries(const Boundary& b);
  scalar compute_phi(const Vector2s& pos) const;
  scalar compute_phi(const Vector2s& pos, const Boundary& b) const;
  
  /*! Boundaries */
  Boundary* root_boundary;
  Boundary* root_sources;
  
  /*! Grid Origin */
  Vector2s origin;
  
  /*! Grid dimensions */
  int ni,nj;
  scalar dx;
  
  /*! Fluid velocity */
  Array2s u, v;
  Array2s temp_u, temp_v;
  Array2s saved_u, saved_v;

  /*! Fluid vorticity */
  Array2s curl;

  /*! Grid mass */
  Array2s u_weight;
  Array2s v_weight;

  /*! Tracer particles */
  std::vector<Particle> particles;
  
  /*! Static geometry representation */
  Array2s nodal_solid_phi;
  Array2s liquid_phi;
  Array2s u_weights, v_weights;
  
  /*! Data arrays for extrapolation */
  Array2c valid, old_valid;
  Array2c u_valid, v_valid;
  
  sorter* m_sorter;
  
  /*! Solver data */
  robertbridson::PCGSolver<scalar> solver;
  robertbridson::SparseMatrix<scalar> matrix;
  std::vector<double> rhs;
  std::vector<double> pressure;

  scalar get_vorticity(const Vector2s& position);
  Vector2s get_velocity(const Vector2s& position);
  Matrix2s get_affine_matrix(const Vector2s& position);
  
  Vector2s get_saved_velocity(const Vector2s& position);
  Matrix2s get_saved_affine_matrix(const Vector2s& position);
  void add_particle(const Particle& position);
  
  /*! P2G scheme */
  void map_p2g();

  /*! different G2P schemes */
  void map_g2p_pic(float dt);
  void map_g2p_apic(float dt);
  void map_g2p_flip_brackbill(float dt, const scalar coeff);
  void map_g2p_flip_bridson(float dt, const scalar coeff);
  void map_g2p_flip_jiang(float dt, const scalar coeff);
  void map_g2p_dapic(float dt);
  Matrix2s construct_dapic_c(Vector2s& position);
  
  void save_velocity();
  void save_bgeo();
  float gridEnergy();

  void compute_cohesion_force();
  void compute_density();
  void compute_normal();
  void correct(scalar dt);
  void resample(Vector2s& p, Vector2s& u, Matrix2s& c);
  void calculateCurl();
  double robust_expm1(double x);
  
  bool draw_grid;
  bool draw_particles;
  bool draw_velocities;
  bool draw_boundaries;
  
protected:
  
  inline scalar circle_phi(const Vector2s& position, const Vector2s& centre, scalar radius) const {
    return ((position-centre).norm() - radius);
  }
  
  inline scalar box_phi(const Vector2s& position, const Vector2s& centre, const Vector2s& expand) const {
    Vector2s center = (centre + (centre + expand)) / 2.;
    scalar dx = fabs(position[0] - center[0]) - expand[0] / 2.;
    scalar dy = fabs(position[1] - center[1]) - expand[1] / 2.;
    scalar dax = max(dx, 0.0);
    scalar day = max(dy, 0.0);
    return min(max(dx, dy), 0.0) + sqrt(dax * dax + day * day);
  }
  
  inline scalar hexagon_phi(const Vector2s& position, const Vector2s& centre, scalar radius) const {
    scalar dx = fabs(position[0] - centre[0]);
    scalar dy = fabs(position[1] - centre[1]);
    return max((dx*0.866025+dy*0.5),dy)-radius;
  }
  
  inline scalar triangle_phi(const Vector2s& position, const Vector2s& centre, scalar radius) const {
    scalar px = position[0] - centre[0];
    scalar py = position[1] - centre[1];
    scalar dx = fabs(px);
    return max(dx*0.866025+py*0.5,-py)-radius*0.5;
  }
  
  inline scalar cylinder_phi(const Vector2s& position, const Vector2s& centre, scalar theta, scalar radius) const {
    Vector2s nhat = Vector2s(cos(theta), sin(theta));
    Vector2s dx = position - centre;
    return sqrt(dx.transpose() * (Matrix2s::Identity() - nhat * nhat.transpose()) * dx) - radius;
  }
  
  inline scalar union_phi(const scalar& d1, const scalar& d2) const {
    return min(d1, d2);
  }
  
  inline scalar intersection_phi(const scalar& d1, const scalar& d2) const {
    return max(d1, d2);
  }
  
  inline scalar substraction_phi(const scalar& d1, const scalar& d2) const {
    return max(-d1, d2);
  }
  
  inline scalar torus_phi(const Vector2s& position, const Vector2s& centre, scalar radius0, scalar radius1) const {
    return max(-circle_phi(position, centre, radius0), circle_phi(position, centre, radius1));
  }
  
  Vector2s trace_rk2(const Vector2s& position, scalar dt);
  
  //tracer particle operations
  
  void particle_boundary_collision(scalar dt);
  
  //fluid velocity operations
  void advect(scalar dt);
  void dapic_advect(scalar dt);
  void add_force(scalar dt);
  
  void project(scalar dt);
  void compute_weights();
  void solve_pressure(scalar dt);
  void compute_phi();
  
  void constrain_velocity();
  
  scalar cfl();
};

#endif
