#ifndef POLY_H
#define POLY_H

#include "Mesh.h"

// Forward declaration
namespace VMACH { struct Polygon3D; }

namespace Poly
{

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Plane;

struct Vertex
{
	Vector3						Position;
	std::vector<int>			NeighborVertexVec;
	int							comp;
	mutable int					ID;

	Vertex();
	Vertex(const Vector3& pos);
	Vertex(const Vector3& pos, const int c);
	Vertex(const Vertex& rhs);

	Vertex& operator=(const Vertex& rhs);
	bool    operator==(const Vertex& rhs) const;
};

typedef	std::vector<Poly::Vertex> Polyhedron;
typedef std::vector<std::vector<int>> Extract;

// Manipulating Polyhedron.
void							InitPolyhedron(Polyhedron& polyhedron, const std::vector<Vector3>& positionVec, const std::vector<std::vector<int>>& neighborVec);
void							Moments(double& zerothMoment, Vector3& firstMoment, const Polyhedron& polyhedron);
Extract*						ExtractFaces(const Polyhedron& polyhedron);
std::vector<std::vector<int>>	ExtractNeighborFromMesh(std::vector<Vector3>& vertices, std::vector<int>& indices);

void							ClipPolyhedron(Polyhedron& polyhedron, const std::vector<Plane>& planes);
Polyhedron						ClipPolyhedron(const Polyhedron& polyhedron, const VMACH::Polygon3D& polygon3D);

void							Translate(Polyhedron& polyhedron, const Vector3& v);
void							Scale(Polyhedron& polyhedron, const Vector3& v);
void							Transform(Polyhedron& polyhedron, const DirectX::XMMATRIX& matrix);

// Helper function.
Polyhedron						GetBB();

void							RenderPolyhedronNormal(std::vector<VertexNormalColor>& vertexData,
													   std::vector<uint32_t>& indexData,
													   const Polyhedron& poly,
													   bool isConvex,
													   Vector3 color = Vector3(0.25f, 0.25f, 0.25f));

void							RenderPolyhedronNormal(std::vector<VertexNormalColor>& vertexData,
													   std::vector<uint32_t>& indexData,
													   const Polyhedron& poly,
													   const Extract* extract,
													   bool isConvex,
													   Vector3 color = Vector3(0.25f, 0.25f, 0.25f));

void							RenderPolyhedron(std::vector<VertexNormalColor>& vertexData, 
												 std::vector<uint32_t>& indexData, 
												 const Polyhedron& poly, 
												 const Extract* extract, 
												 bool isConvex = true,
												 Vector3 color = Vector3(0.25f, 0.25f, 0.25f));

int								ComparePlanePoint(const Plane& plane, const Vector3& point);
int								ComparePlaneBB(const Plane& plane, const double xmin, const double ymin, const double zmin, const double xmax, const double ymax, const double zmax);
Vector3							PlaneLineIntersection(const typename Vector3& a, const typename Vector3& b, const Plane& plane);

// Triangulization
bool							IsCCW(const Polyhedron& polyhedron, const std::vector<int>& face, const Vector3& normal);
std::vector<int>				EarClipping(const Polyhedron& polyhedron, const std::vector<int>& face);
};

#endif
