#ifndef VMACH_H
#define VMACH_H

#include "Mesh.h"

namespace VMACH
{
	using DirectX::SimpleMath::Plane;
	using DirectX::SimpleMath::Vector3;

	struct PolygonEdge
	{
	public:
		std::vector<Vector3>		VertexVec;
	};

	struct PolygonFace
	{
	public:
		std::vector<Vector3>		VertexVec;
		Plane						FacePlane;

		PolygonFace() = default;
		PolygonFace(std::vector<Vector3> _vertexVec) : 
			VertexVec(_vertexVec), 
			FacePlane(Plane(_vertexVec[0], _vertexVec[1], _vertexVec[2])) {}

		bool operator==(const PolygonFace& other);

		bool IsEmpty() const;
		double CalcDistanceToPoint(const Vector3& point) const;
		Vector3 GetIntersectionPoint(const Vector3& p1, const Vector3& p2) const;
		Vector3 GetCentriod() const;

		void Render(
			std::vector<VertexNormalColor>& vertexData,
			std::vector<uint32_t>& indexData,
			const Vector3& color = { 0.25f, 0.25f, 0.25f }) const;

		void AddVertex(Vector3 vertex);
		void Rewind();
		void Reorder();

		static PolygonFace ClipMeshFace(const PolygonFace& inFace, const PolygonFace& clippingFace, std::vector<PolygonEdge>& edgeVec);
		static PolygonFace ClipFace(const PolygonFace& inFace, const PolygonFace& clippingFace, std::vector<Vector3>& intersectPointVec);
	};

	struct Polygon3D
	{
	public:
		std::vector<PolygonFace>	FaceVec;

		Polygon3D() = default;
		Polygon3D(std::vector<PolygonFace> _faceVec) : FaceVec(_faceVec) {}

		Vector3 GetCentroid() const;
		bool Contains(const Vector3& point) const;
		
		void Render(
			std::vector<VertexNormalColor>&	vertexData, 
			std::vector<uint32_t>& indexData, 
			const Vector3& color = { 0.25f, 0.25f, 0.25f }) const;
		
		void Translate(const Vector3& vector);
		void Scale(const float& scalar);
		void Scale(const Vector3& vector);

		static Polygon3D ClipMesh(const Polygon3D& mesh, const Polygon3D& clippingPolygon);
		static Polygon3D ClipMeshFace(const Polygon3D& mesh, const PolygonFace& clippingFace);
		static Polygon3D ClipPolygon(const Polygon3D& inPolygon, const Polygon3D& clippingPolygon);
		static Polygon3D ClipFace(const Polygon3D& inPolygon, const PolygonFace& clippingFace);
	};

	struct ConvexHullVertex : public Vector3
	{
	public:
		bool						Processed;

		ConvexHullVertex() : Vector3(), Processed(false) {}
		ConvexHullVertex(float ix, float iy, float iz) : Vector3(ix, iy, iz), Processed(false) {}
		ConvexHullVertex(const XMFLOAT3& f3) : ConvexHullVertex(f3.x, f3.y, f3.z) {}
		ConvexHullVertex(const Vector3& v3) : Vector3(v3), Processed(false) {}
	};

	struct ConvexHullFace
	{
	public:
		bool						Visible;
		ConvexHullVertex			Vertices[3];

		ConvexHullFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3);

		void Rewind();
		float CalcArea();
	};

	struct ConvexHullEdge
	{
	public:
		bool						Remove;
		ConvexHullFace*				Face1;
		ConvexHullFace*				Face2;
		ConvexHullVertex			EndPoints[2];

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

		void Render(
			std::vector<VertexNormalColor>& vertexData, 
			std::vector<uint32_t>& indexData, 
			DirectX::SimpleMath::Vector3 color = { 0.25f, 0.25f, 0.25f }) const;

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

	Polygon3D GetBoxPolygon();
};

#endif