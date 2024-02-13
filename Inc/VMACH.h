#ifndef VMACH_H
#define VMACH_H

#include "Mesh.h"

namespace VMACH
{
	using DirectX::SimpleMath::Plane;
	using DirectX::SimpleMath::Vector3;

	struct PolygonEdge
	{
		std::vector<Vector3>		VertexVec;
	};

	// If convex is not guranteed, face plane is NOT automatically generated.
	struct PolygonFace
	{
		bool						GuaranteeConvex;
		std::vector<Vector3>		VertexVec;
		Plane						FacePlane;
		bool						FacePlaneConstructed;
		bool						ForceColor;
		std::vector<int>			AdjFaceVec;

		PolygonFace(bool _guranteeConvex) : 
			GuaranteeConvex(_guranteeConvex), FacePlaneConstructed(false), ForceColor(false) {}
		PolygonFace(bool _guranteeConvex, std::vector<Vector3> _vertexVec) : 
			GuaranteeConvex(_guranteeConvex), VertexVec(_vertexVec), ForceColor(false) { ConstructFacePlane(); }

		bool operator==(const PolygonFace& other);

		bool IsEmpty() const;
		bool IsCCW(const Vector3& normal) const;
		bool IsConvex(const Vector3& normal) const;
		
		double CalcDistanceToPoint(const Vector3& point) const;
		Vector3 GetIntersectionPoint(const Vector3& p1, const Vector3& p2) const;
		Vector3 GetCentriod() const;
		Vector3 GetNormal() const;

		void Render(
			std::vector<VertexNormalColor>& vertexData,
			std::vector<uint32_t>& indexData,
			const Vector3& color = { 0.25f, 0.25f, 0.25f }) const;
		std::vector<int> EarClipping() const;

		void AddVertex(const Vector3& newVertex);
		void ConstructFacePlane();
		void ManuallySetFacePlane(const Plane& plane);
		
		void Rewind();
		void __Reorder();

		static PolygonFace ClipWithPlane(const PolygonFace& inFace, const Plane& clippingPlane, std::vector<PolygonEdge>& edgeVec);
		static PolygonFace ClipWithFace(const PolygonFace& inFace, const PolygonFace& clippingFace, std::vector<PolygonEdge>& edgeVec);
	};

	struct Polygon3D
	{
		bool						GuaranteeConvex;
		std::vector<PolygonFace>	FaceVec;

		Polygon3D(bool _guranteeConvex) : GuaranteeConvex(_guranteeConvex) {}
		Polygon3D(bool _guranteeConvex, std::vector<PolygonFace> _faceVec) : GuaranteeConvex(_guranteeConvex), FaceVec(_faceVec) {}

		Vector3 GetCentroid() const;
		bool Contains(const Vector3& point) const;
		
		void Render(
			std::vector<VertexNormalColor>&	vertexData, 
			std::vector<uint32_t>& indexData, 
			const Vector3& color = { 0.25f, 0.25f, 0.25f }) const;
		
		void AddFace(const PolygonFace& newFace);

		void Translate(const Vector3& vector);
		void Scale(const float& scalar);
		void Scale(const Vector3& vector);

		static Polygon3D ClipWithPlane(const Polygon3D& inPolygon, const Plane& clippingPlane);
		static Polygon3D ClipWithFace(const Polygon3D& inPolygon, const PolygonFace& clippingFace, int doTest = 0);
		static Polygon3D ClipWithPolygon(const Polygon3D& inPolygon, const Polygon3D& clippingPolygon);
	};

	struct ConvexHullVertex : public Vector3
	{
		bool						Processed;

		ConvexHullVertex() : Vector3(), Processed(false) {}
		ConvexHullVertex(float ix, float iy, float iz) : Vector3(ix, iy, iz), Processed(false) {}
		ConvexHullVertex(const XMFLOAT3& f3) : ConvexHullVertex(f3.x, f3.y, f3.z) {}
		ConvexHullVertex(const Vector3& v3) : Vector3(v3), Processed(false) {}
	};

	struct ConvexHullFace
	{
		bool						Visible;
		ConvexHullVertex			Vertices[3];

		ConvexHullFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3);

		void Rewind();
		float CalcArea();
	};

	struct ConvexHullEdge
	{
		bool						Remove;
		ConvexHullFace*				Face1;
		ConvexHullFace*				Face2;
		ConvexHullVertex			EndPoints[2];

		ConvexHullEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2);

		void LinkFace(ConvexHullFace* face);
		void EraseFace(ConvexHullFace* face);
	};

	class ConvexHull
	{
	public:
		ConvexHull(const std::vector<ConvexHullVertex>& pointCloud, uint32_t limitCnt);
		ConvexHull(const std::vector<Vector3>& pointCloud, uint32_t limitCnt);
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

	struct KdopElement
	{
		Vector3 Normal;
		Vector3 MinVertex;
		Vector3 MaxVertex;
		double MinDist = DBL_MAX;
		double MaxDist = -DBL_MAX;
		Plane MinPlane;
		Plane MaxPlane;

		KdopElement(const Vector3& _normal) : Normal(_normal) {}
	};

	struct Kdop
	{
		std::vector<KdopElement> elementVec;

		Kdop(const std::vector<Vector3>& normalVec)
		{
			std::transform(normalVec.begin(), normalVec.end(), std::back_inserter(elementVec), [](const Vector3& normal) { return KdopElement(normal); });
		}

		void Calc(const std::vector<ConvexHullVertex>& vertices, const double& maxAxisScale, const float& planeGapInv);
		void Calc(const Polygon3D& mesh);
		Polygon3D ClipWithPolygon(const Polygon3D& polygon) const;
	};

	struct Compound
	{
		Polygon3D convexCell;
		Polygon3D visualMesh;

		Compound() : convexCell({ true }), visualMesh({ false }) {}
	};

	bool NearlyEqual(const Vector3& v1, const Vector3& v2);
	Polygon3D GetBoxPolygon();
	float GetAngleBetweenTwoVectorsOnPlane(const Vector3& v1, const Vector3& v2, const Vector3& n);
	bool OnYourRight(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& n);
	void RenderEdge(std::vector<VertexNormalColor>& vertexData, std::vector<uint32_t>& indexData);
};

#endif