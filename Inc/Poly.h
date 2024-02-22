#ifndef POLY_H
#define POLY_H

namespace Poly
{
using DirectX::SimpleMath::Vector3;
using namespace std;

struct Plane
{
	double  D;
	Vector3 Normal;
	size_t  ID;

	Plane();
	Plane(const double d, const Vector3& normal);
	Plane(const double d, const Vector3& normal, const size_t id);
	Plane(const Vector3& p, const Vector3& normal);
	Plane(const Vector3& p, const Vector3& normal, const size_t id);
	Plane(const Plane& rhs);

	Plane& operator=(const Plane& rhs);

	bool operator==(const Plane& rhs) const;
	bool operator!=(const Plane& rhs) const;
	bool operator<(const Plane& rhs) const;
	bool operator>(const Plane& rhs) const;
};

struct Vertex
{
	Vector3               Position;
	std::vector<int>      NeighborVertexVec;
	int                   comp;
	mutable int           ID;
	mutable std::set<size_t> clips;

	Vertex();
	Vertex(const Vector3& pos);
	Vertex(const Vector3& pos, const int c);
	Vertex(const Vertex& rhs);

	Vertex& operator=(const Vertex& rhs);
	bool    operator==(const Vertex& rhs) const;
};

typedef std::vector<Poly::Vertex> Polyhedron;

int nextInFaceLoop(const Vertex& v, const int vprev);

void InitPolyhedron(Polyhedron& polyhedron, const std::vector<Vector3>& positionVec,
					const std::vector<std::vector<int>>& neighborVec);

std::vector<std::vector<int>> ExtractFaces(const Polyhedron& polyhedron);

void Moments(double& zerothMoment, Vector3& firstMoment, const std::vector<Vertex>& polyhedron);

int Compare(const Plane& plane, const Vector3& point);
int Compare(const Plane& plane, const double xmin, const double ymin, const double zmin, const double xmax,
			const double ymax, const double zmax);

Vector3 segmentPlaneIntersection(const typename Vector3& a, const typename Vector3& b, const Plane& plane);

void ClipPolyhedron(std::vector<Vertex>& polyhedron, const std::vector<Plane>& planes);

void collapseDegenerates(std::vector<Vertex>& polyhedron, const double tol);

std::vector<std::vector<int>> ExtractNeighborFromMesh(std::vector<Vector3>& vertices, std::vector<int>& indices);
}; // namespace Poly

#endif
