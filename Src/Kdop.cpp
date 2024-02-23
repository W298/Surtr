#include "pch.h"
#include "Kdop.h"

#include "VMACH.h"
#include "Poly.h"

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Plane;

Kdop::KdopContainer::KdopContainer(const std::vector<Vector3>& normalVec)
{
	std::transform(normalVec.begin(), normalVec.end(), std::back_inserter(ElementVec), [](const Vector3& normal) { return KdopElement(normal); });
}

void Kdop::KdopContainer::Calc(const std::vector<Vector3>& vertices, const double& maxAxisScale, const float& planeGapInv)
{
	for (const Vector3& vert : vertices)
	{
		for (KdopElement& kdopElement : ElementVec)
		{
			float t = vert.Dot(kdopElement.Normal);

			if (kdopElement.MinDist > t)
			{
				kdopElement.MinDist = t;
				kdopElement.MinPlane = Plane(vert, -kdopElement.Normal);
				kdopElement.MinVertex = vert;
			}

			if (kdopElement.MaxDist < t)
			{
				kdopElement.MaxDist = t;
				kdopElement.MaxPlane = Plane(vert, kdopElement.Normal);
				kdopElement.MaxVertex = vert;
			}
		}
	}

	for (KdopElement& kdopElement : ElementVec)
	{
		Vector3 minPlaneNormal = kdopElement.MinPlane.Normal();
		Vector3 maxPlaneNormal = kdopElement.MaxPlane.Normal();
		minPlaneNormal.Normalize();
		maxPlaneNormal.Normalize();

		kdopElement.MinPlane =
			Plane(kdopElement.MinVertex + minPlaneNormal * (maxAxisScale / planeGapInv), minPlaneNormal);
		kdopElement.MaxPlane =
			Plane(kdopElement.MaxVertex + maxPlaneNormal * (maxAxisScale / planeGapInv), maxPlaneNormal);
	}
}

void Kdop::KdopContainer::Calc(const VMACH::Polygon3D& mesh)
{
	for (const VMACH::PolygonFace& face : mesh.FaceVec)
	{
		for (const Vector3& vert : face.VertexVec)
		{
			for (KdopElement& kdopElement : ElementVec)
			{
				float t = vert.Dot(kdopElement.Normal);

				if (kdopElement.MinDist > t)
				{
					kdopElement.MinDist = t;
					kdopElement.MinPlane = Plane(vert, -kdopElement.Normal);
					kdopElement.MinVertex = vert;
				}

				if (kdopElement.MaxDist < t)
				{
					kdopElement.MaxDist = t;
					kdopElement.MaxPlane = Plane(vert, kdopElement.Normal);
					kdopElement.MaxVertex = vert;
				}
			}
		}
	}

	for (KdopElement& kdopElement : ElementVec)
	{
		Vector3 minPlaneNormal = kdopElement.MinPlane.Normal();
		Vector3 maxPlaneNormal = kdopElement.MaxPlane.Normal();
		minPlaneNormal.Normalize();
		maxPlaneNormal.Normalize();

		kdopElement.MinPlane = Plane(kdopElement.MinVertex + minPlaneNormal * 0.001, minPlaneNormal);
		kdopElement.MaxPlane = Plane(kdopElement.MaxVertex + maxPlaneNormal * 0.001, maxPlaneNormal);
	}
}

void Kdop::KdopContainer::Calc(const Poly::Polyhedron& mesh)
{
	for (const Poly::Vertex& vert : mesh)
	{
		for (KdopElement& kdopElement : ElementVec)
		{
			float t = vert.Position.Dot(kdopElement.Normal);

			if (kdopElement.MinDist > t)
			{
				kdopElement.MinDist = t;
				kdopElement.MinPlane = Plane(vert.Position, -kdopElement.Normal);
				kdopElement.MinVertex = vert.Position;
			}

			if (kdopElement.MaxDist < t)
			{
				kdopElement.MaxDist = t;
				kdopElement.MaxPlane = Plane(vert.Position, kdopElement.Normal);
				kdopElement.MaxVertex = vert.Position;
			}
		}
	}
}

VMACH::Polygon3D Kdop::KdopContainer::ClipWithPolygon(const VMACH::Polygon3D& polygon, int doTest) const
{
	VMACH::Polygon3D outPolygon = polygon;

	for (int i = 0; i < ElementVec.size(); i++)
	{
		Vector3 n = ElementVec[i].MinPlane.Normal();
		n.Normalize();

		bool exist =
			polygon.FaceVec.end() != std::find_if(
				polygon.FaceVec.begin(),
				polygon.FaceVec.end(),
				[&](const VMACH::PolygonFace& f)
				{
					Vector3 fn = f.GetNormal();
					return (1 - fn.Dot(n)) < 1e-4 && std::abs(f.FacePlane.w - ElementVec[i].MinPlane.w) < 1e-4;
				});

		if (TRUE == exist)
			continue;

		outPolygon = VMACH::Polygon3D::ClipWithPlane(outPolygon, ElementVec[i].MinPlane, (doTest == 1 && i == 5) ? 1 : -1);
	}

	for (int i = 0; i < ElementVec.size(); i++)
	{
		Vector3 n = ElementVec[i].MaxPlane.Normal();
		n.Normalize();

		bool exist =
			polygon.FaceVec.end() != std::find_if(
				polygon.FaceVec.begin(),
				polygon.FaceVec.end(),
				[&](const VMACH::PolygonFace& f)
				{
					Vector3 fn = f.GetNormal();
					return (1 - fn.Dot(n)) < 1e-4 && std::abs(f.FacePlane.w - ElementVec[i].MaxPlane.w) < 1e-4;
				});

		if (TRUE == exist)
			continue;

		outPolygon = VMACH::Polygon3D::ClipWithPlane(outPolygon, ElementVec[i].MaxPlane);
	}

	return outPolygon;
}

Poly::Polyhedron Kdop::KdopContainer::ClipWithPolyhedron(const Poly::Polyhedron& polyhedron)
{
	std::vector<Plane> planes;
	for (int x = 0; x < ElementVec.size(); x++)
	{
		planes.push_back(ElementVec[x].MinPlane);
		planes.push_back(ElementVec[x].MaxPlane);
	}

	Poly::Polyhedron res = polyhedron;
	Poly::ClipPolyhedron(res, planes);

	return res;
}

void Kdop::KdopContainer::Render(std::vector<VertexNormalColor>& vertexData, std::vector<uint32_t>& indexData)
{
	const auto collectPolygonFaces = [&](Plane p, Vector3 x)
	{
		VMACH::PolygonFace cf = { true };

		Vector3 n = p.Normal();
		n.Normalize();

		Vector3 tmp(1, 2, 3);
		Vector3 u = n.Cross(tmp);
		u.Normalize();

		Vector3 v = u.Cross(n);
		v.Normalize();

		// #CORRECTION
		Vector3 p1 = x + u * 0.3 - v * 0.3;
		Vector3 p2 = x + u * 0.3 + v * 0.3;
		Vector3 p3 = x - u * 0.3 + v * 0.3;
		Vector3 p4 = x - u * 0.3 - v * 0.3;

		cf.AddVertex(p1);
		cf.AddVertex(p2);
		cf.AddVertex(p3);
		cf.AddVertex(p4);
		cf.Rewind();
		return cf;
	};

	for (int i = 0; i < ElementVec.size(); i++)
	{
		// #BREAK
		if (i != 5)
			continue;

		auto f1 = collectPolygonFaces(ElementVec[i].MinPlane, ElementVec[i].MinVertex);
		auto f2 = collectPolygonFaces(ElementVec[i].MaxPlane, ElementVec[i].MaxVertex);

		f1.Render(vertexData, indexData);
		f2.Render(vertexData, indexData);
	}
}