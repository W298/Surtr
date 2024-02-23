#ifndef KDOP_H
#define KDOP_H

#include "Mesh.h"

// Forward declaration
namespace VMACH { struct Polygon3D; }
namespace Poly { struct Vertex; typedef std::vector<Poly::Vertex> Polyhedron; }

namespace Kdop
{

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Plane;

struct KdopElement
{
	Vector3 Normal;
	Vector3 MinVertex;
	Vector3 MaxVertex;
	double  MinDist = DBL_MAX;
	double  MaxDist = -DBL_MAX;
	Plane   MinPlane;
	Plane   MaxPlane;

	KdopElement(const Vector3& _normal) : Normal(_normal) {}
};

struct KdopContainer
{
	std::vector<KdopElement> ElementVec;

	KdopContainer(const std::vector<Vector3>& normalVec);

	void				Calc(const std::vector<Vector3>& vertices, const double& maxAxisScale, const float& planeGapInv);
	void				Calc(const VMACH::Polygon3D& mesh);
	void				Calc(const Poly::Polyhedron& mesh);
	VMACH::Polygon3D	ClipWithPolygon(const VMACH::Polygon3D& polygon, int doTest = -1) const;
	Poly::Polyhedron	ClipWithPolyhedron(const Poly::Polyhedron& polyhedron);
	void				Render(std::vector<VertexNormalColor>& vertexData, std::vector<uint32_t>& indexData);
};

}

#endif