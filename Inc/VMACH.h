#ifndef VMACH_H
#define VMACH_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <list>

#include "SimpleMath.h"

namespace VMACH
{
	struct Point3D : DirectX::SimpleMath::Vector3
	{
		bool processed;

		Point3D() : DirectX::SimpleMath::Vector3(), processed(false) {}
		Point3D(float ix, float iy, float iz) : DirectX::SimpleMath::Vector3(ix, iy, iz), processed(false) {}
		Point3D(const XMFLOAT3& f3) : Point3D(f3.x, f3.y, f3.z) {}
		Point3D(const DirectX::SimpleMath::Vector3& vec) : DirectX::SimpleMath::Vector3(vec), processed(false) {}
	};

	struct Face
	{
		bool visible;
		Point3D vertices[3];

		Face(const Point3D& p1, const Point3D& p2, const Point3D& p3) : visible(false)
		{
			vertices[0] = p1; vertices[1] = p2; vertices[2] = p3;
		};

		void Reverse()
		{
			std::swap(vertices[0], vertices[2]);
		};

		float CalcArea()
		{
			Point3D d1 = vertices[1] - vertices[0];
			Point3D d2 = vertices[2] - vertices[0];

			return 0.5f * d1.Cross(d2).Length();
		}
	};

	struct Edge
	{
		bool remove;
		Face* adjface1;
		Face* adjface2;
		Point3D endpoints[2];

		Edge(const Point3D& p1, const Point3D& p2) : adjface1(nullptr), adjface2(nullptr), remove(false)
		{
			endpoints[0] = p1; endpoints[1] = p2;
		};

		void LinkAdjFace(Face* face)
		{
			if (adjface1 != nullptr && adjface2 != nullptr) return;
			(adjface1 == nullptr ? adjface1 : adjface2) = face;
		};

		void Erase(Face* face)
		{
			if (adjface1 != face && adjface2 != face) return;
			(adjface1 == face ? adjface1 : adjface2) = nullptr;
		};
	};

	struct PointHash
	{
		std::size_t operator() (const Point3D& p) const
		{
			std::string sx, sy, sz;
			sx = std::to_string(p.x);
			sy = std::to_string(p.y);
			sz = std::to_string(p.z);
			return std::hash<std::string>{}(sx + sy + sz);
		}
	};

	class ConvexHull
	{
	public:
		ConvexHull(const std::vector<Point3D>& pointCloud, uint32_t limitCnt);
		~ConvexHull() = default;

		bool Contains(const Point3D& p) const;
		const std::list<Face> GetFaces() const;
		const std::vector<Point3D> GetVertices() const;

		static bool Colinear(const Point3D& a, const Point3D& b, const Point3D& c);
		static float Volume(const Face& f, const Point3D& p);
		static int VolumeSign(const Face& f, const Point3D& p);

	private:
		static size_t Key2Edge(const Point3D& a, const Point3D& b);
		static Point3D FindInnerPoint(const Face* f, const Edge& e);
		
		void CreateFace(const Point3D& a, const Point3D& b, const Point3D& c, const Point3D& innerPoint);
		void CreateEdge(const Point3D& p1, const Point3D& p2, Face& newFace);
		
		void AddPointToHull(const Point3D& pt);
		bool BuildFirstHull();
		void ConstructHull();
		
		void CleanUp();
		
		void ExtractExteriorPoints();	

		std::vector<Face*>                m_visibleFaceVec = {};
		std::vector<Face*>                m_addedFaceVec = {};

		uint32_t						  m_limitCnt = 0;
		uint32_t                          m_processedPointCnt = 0;

		std::vector<Point3D>              m_pointCloud = {};
		std::vector<float>                m_pointVolume = {};

		std::vector<Point3D>              m_exteriorVertices = {};
		std::list<Face>                   m_faceList = {};
		std::list<Edge>                   m_edgeList = {};
		std::unordered_map<size_t, Edge*> m_edgeMap = {};
	};
};

#endif