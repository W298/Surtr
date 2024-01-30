#ifndef VMACH_H
#define VMACH_H

namespace VMACH
{
	struct PolygonFace
	{
		std::vector<DirectX::SimpleMath::Vector3> vertexVec;
		DirectX::SimpleMath::Plane plane;

		PolygonFace() = default;
		PolygonFace(std::vector<DirectX::SimpleMath::Vector3> _vertexVec) : 
			vertexVec(_vertexVec), plane(DirectX::SimpleMath::Plane(_vertexVec[0], _vertexVec[1], _vertexVec[2])) {}

		bool IsEmpty();
		double CalcDistanceToPoint(const DirectX::SimpleMath::Vector3& point) const;
		DirectX::SimpleMath::Vector3 GetIntersectionPoint(const DirectX::SimpleMath::Vector3& p1, const DirectX::SimpleMath::Vector3& p2) const;

		void AddVertex(DirectX::SimpleMath::Vector3 vertex);
		void Rewind();

		static PolygonFace ClipFace(const PolygonFace& inFace, const PolygonFace& clippingFace);
	};

	struct Polygon3D
	{
		std::vector<PolygonFace> faceVec;

		static Polygon3D ClipPolygon(const Polygon3D& inPolygon, const Polygon3D& clippingPolygon);
		static Polygon3D ClipFace(const Polygon3D& inPolygon, const PolygonFace& clippingFace);
	};

	struct ConvexHullVertex : DirectX::SimpleMath::Vector3
	{
		bool processed;

		ConvexHullVertex() : DirectX::SimpleMath::Vector3(), processed(false) {}
		ConvexHullVertex(float ix, float iy, float iz) : DirectX::SimpleMath::Vector3(ix, iy, iz), processed(false) {}
		ConvexHullVertex(const XMFLOAT3& f3) : ConvexHullVertex(f3.x, f3.y, f3.z) {}
		ConvexHullVertex(const DirectX::SimpleMath::Vector3& vec) : DirectX::SimpleMath::Vector3(vec), processed(false) {}
	};

	struct ConvexHullFace
	{
		bool visible;
		ConvexHullVertex vertices[3];

		ConvexHullFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3);

		void Rewind();
		float CalcArea();
	};

	struct ConvexHullEdge
	{
		bool remove;
		ConvexHullFace* face1;
		ConvexHullFace* face2;
		ConvexHullVertex endPoints[2];

		ConvexHullEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2);

		void LinkFace(ConvexHullFace* face);
		void EraseFace(ConvexHullFace* face);
	};

	struct PointHash
	{
		std::size_t operator() (const ConvexHullVertex& point) const;
	};

	class ConvexHull
	{
	public:
		ConvexHull(const std::vector<ConvexHullVertex>& pointCloud, uint32_t limitCnt);
		~ConvexHull() = default;

		bool Contains(const ConvexHullVertex& point) const;
		const std::list<ConvexHullFace> GetFaces() const;
		const std::list<ConvexHullEdge> GetEdges() const;

		static bool Colinear(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3);
		static float Volume(const ConvexHullFace& face, const ConvexHullVertex& point);

	private:
		static size_t Key2Edge(const ConvexHullVertex& p1, const ConvexHullVertex& p2);
		static ConvexHullVertex FindInnerPoint(const ConvexHullFace* face, const ConvexHullEdge& edge);
		
		void CreateFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3, const ConvexHullVertex& innerPoint);
		void CreateEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2, ConvexHullFace& newFace);
		
		void AddPointToHull(const ConvexHullVertex& point);
		bool BuildFirstHull();
		void CreateConvexHull();
		
		void CleanUp();
		
		std::vector<ConvexHullFace*>                         m_visibleFaceVec = {};
		std::vector<ConvexHullFace*>                         m_addedFaceVec = {};

		uint32_t						                     m_limitCnt = 0;
		uint32_t                                             m_processedPointCnt = 0;

		std::vector<ConvexHullVertex>                        m_pointCloud = {};
		std::vector<float>                                   m_pointVolume = {};

		std::list<ConvexHullFace>                            m_faceList = {};
		std::list<ConvexHullEdge>                            m_edgeList = {};
		std::unordered_map<size_t, ConvexHullEdge*>          m_edgeMap = {};
	};
};

#endif