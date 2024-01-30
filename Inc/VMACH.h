#ifndef VMACH_H
#define VMACH_H

namespace VMACH
{
	struct PolygonFace
	{
		std::vector<DirectX::SimpleMath::Vector3> vertexVec;

		PolygonFace() = default;
		PolygonFace(std::vector<DirectX::SimpleMath::Vector3> _vertexVec) : vertexVec(_vertexVec) {}

		bool IsEmpty();
		bool IsPointInsideFace(DirectX::SimpleMath::Vector3 point);

		int GetNumberOfEdges();
		double GetPointVsFaceDeterminant(DirectX::SimpleMath::Vector3 point);
		DirectX::SimpleMath::Vector3 GetIntersectionPoint(DirectX::SimpleMath::Vector3 p1, DirectX::SimpleMath::Vector3 p2);

		void AddVertex(DirectX::SimpleMath::Vector3 vertex);
		void Rewind();

		PolygonFace ClipFace(PolygonFace clippingFace);
	};

	struct Polygon3D
	{
		std::vector<PolygonFace> faceVec;

		Polygon3D ClipPolygon(Polygon3D clippingPolygon);
		Polygon3D ClipFace(Polygon3D inPolygon, PolygonFace clippingFace);
		Polygon3D ClipFaces(Polygon3D inPolygon, std::vector<PolygonFace> clippingFaceVec);
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
		std::size_t operator() (const ConvexHullVertex& p) const;
	};

	class ConvexHull
	{
	public:
		ConvexHull(const std::vector<ConvexHullVertex>& pointCloud, uint32_t limitCnt);
		~ConvexHull() = default;

		bool Contains(const ConvexHullVertex& p) const;
		const std::list<ConvexHullFace> GetFaces() const;
		const std::list<ConvexHullEdge> GetEdges() const;
		const std::vector<ConvexHullVertex> GetVertices() const;

		static bool Colinear(const ConvexHullVertex& a, const ConvexHullVertex& b, const ConvexHullVertex& c);
		static float Volume(const ConvexHullFace& f, const ConvexHullVertex& p);
		static int VolumeSign(const ConvexHullFace& f, const ConvexHullVertex& p);

	private:
		static size_t Key2Edge(const ConvexHullVertex& a, const ConvexHullVertex& b);
		static ConvexHullVertex FindInnerPoint(const ConvexHullFace* f, const ConvexHullEdge& e);
		
		void CreateFace(const ConvexHullVertex& a, const ConvexHullVertex& b, const ConvexHullVertex& c, const ConvexHullVertex& innerPoint);
		void CreateEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2, ConvexHullFace& newFace);
		
		void AddPointToHull(const ConvexHullVertex& pt);
		bool BuildFirstHull();
		void ConstructHull();
		
		void CleanUp();
		
		void ExtractExteriorPoints();

		std::vector<ConvexHullFace*>                         m_visibleFaceVec = {};
		std::vector<ConvexHullFace*>                         m_addedFaceVec = {};

		uint32_t						                     m_limitCnt = 0;
		uint32_t                                             m_processedPointCnt = 0;

		std::vector<ConvexHullVertex>                        m_pointCloud = {};
		std::vector<float>                                   m_pointVolume = {};

		std::vector<ConvexHullVertex>                        m_exteriorVertices = {};
		std::list<ConvexHullFace>                            m_faceList = {};
		std::list<ConvexHullEdge>                            m_edgeList = {};
		std::unordered_map<size_t, ConvexHullEdge*>          m_edgeMap = {};
	};
};

#endif