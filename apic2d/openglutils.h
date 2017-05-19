#ifndef OPENGL_UTILS_H
#define OPENGL_UTILS_H

#include <vector>
#include "MathDefs.h"
#include "array2.h"

void draw_circle2d(const Vector2s& centre, scalar rad, int segs);
void draw_grid2d(const Vector2s& origin, scalar dx, int nx, int ny);
void draw_box2d(const Vector2s& origin, scalar width, scalar height);
void draw_segmentset2d(const std::vector<Vector2s>& vertices, const std::vector<Vector2i>& edges);
void draw_points2d(const std::vector<Vector2s>& points);
void draw_polygon2d(const std::vector<Vector2s>& vertices);
void draw_polygon2d(const std::vector<Vector2s>& vertices, const std::vector<int>& order);
void draw_segment2d(const Vector2s& start, const Vector2s& end);
void draw_arrow2d(const Vector2s& start, const Vector2s& end, scalar arrow_head_len);
void draw_grid_data2d(Array2s& data, Vector2s origin, scalar dx, bool color = false);
void draw_trimesh2d(const std::vector<Vector2s>& vertices, const std::vector<Vector3i>& tris);    
   
void draw_trimesh3d(const std::vector<Vector3s>& vertices, const std::vector<Vector3i>& tris);
void draw_trimesh3d(const std::vector<Vector3s>& vertices, const std::vector<Vector3i>& tris, const std::vector<Vector3s>& normals);
void draw_box3d(const Vector3s& dimensions);

#endif