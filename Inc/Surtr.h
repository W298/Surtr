#pragma once

#include <PxPhysicsAPI.h>

#include "ShadowMap.h"
#include "Mesh.h"

#include "VMACH.h"
#include "Poly.h"
#include "Kdop.h"

using namespace DirectX;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Ray;

class Surtr
{
public:

	Surtr() noexcept;
	~Surtr();

	Surtr(Surtr&&) = default;
	Surtr& operator= (Surtr&&) = default;

	Surtr(Surtr const&) = delete;
	Surtr& operator= (Surtr const&) = delete;

	// Initialization
	void InitializeD3DResources(HWND window, int width, int height, UINT modelIndex, UINT shadowMapSize, BOOL fullScreenMode);

	// Basic game loop
	void Tick();

	// Input handle
	void OnKeyDown(UINT8 key);
	void OnKeyUp(UINT8 key);
	void OnMouseWheel(float delta);
	void OnMouseMove(int x, int y);
	void OnMouseDown();

	// Messages
	void OnActivated();
	void OnDeactivated();
	void OnSuspending();
	void OnResuming();
	void OnWindowSizeChanged(int width, int height);

private:

	struct MeshSB
	{
		XMMATRIX	WorldMatrix;
	};

	struct OpaqueCB
	{
		XMMATRIX	ViewProjMatrix;
		XMFLOAT4	CameraPosition;
		XMFLOAT4	LightDirection;
		XMFLOAT4	LightColor;
		XMMATRIX	ShadowTransform;
		uint8_t		Padding[80];
	};

	struct ShadowCB
	{
		XMMATRIX	LightWorldMatrix;
		XMMATRIX	LightViewProjMatrix;
		XMVECTOR	CameraPosition;
		uint8_t		Padding[104];
	};

	struct FractureArgs
	{
		INT			ICHIncludePointLimit = 20;
		FLOAT		ACHPlaneGapInverse = 2000.0f;
		INT			Seed = 46354;
		XMFLOAT3	ImpactPosition = DirectX::XMFLOAT3(0, 6, 4);
		FLOAT		ImpactRadius = 1.0f;
		FLOAT		FracturePatternDist = 0.01f;
		bool		PartialFracture = true;
		INT			InitialDecomposeCellCnt = 64;
		INT			FracturePatternCellCnt = 64;
	};

	struct FractureResult
	{
		UINT                ICHFaceCnt = 0;
		UINT                ACHErrorPointCnt = 0;
	};

	struct Piece
	{
		Poly::Polyhedron	Convex;
		Poly::Polyhedron	Mesh;
	};

	struct CompoundInfo
	{
		std::vector<Piece>							PieceVec;
		std::vector<std::vector<std::vector<int>>>	PieceExtractedConvex;
		std::vector<std::set<int>>					CompoundBind;
	};

	struct FractureStorage
	{
		CompoundInfo					InitialCompound;
		std::vector<VMACH::Polygon3D>	FracturePattern;
		Vector3							BBCenter;
		Vector3							MinBB;
		Vector3							MaxBB;
		float							MaxAxisScale;
	};

	void Update(DX::StepTimer const& timer);
	void UploadMesh();
	void UploadStructuredBuffer();
	void Render();

	void CreateDeviceResources();
	void CreateDeviceDependentResources();
	void CreateWindowSizeDependentResources();
	void CreateCommandListDependentResources();

	void WaitForGpu() noexcept;
	void MoveToNextFrame();
	void GetAdapter(IDXGIAdapter1** ppAdapter) const;

	void OnDeviceLost();

	// Core feature functions
	void							PrepareFracture(_In_ const std::vector<VertexNormalColor>& visualMeshVertices,
													_In_ const std::vector<uint32_t>& visualMeshIndices);
	
	void							DoFracture(_Out_ std::vector<VertexNormalColor>& fracturedVertexData,
											   _Out_ std::vector<uint32_t>& fracturedIndexData);

	std::vector<Vector3>			GenerateICHNormal(_In_ const std::vector<Vector3>& vertices, _In_ const int ichIncludePointLimit);
	std::vector<Vector3>			GenerateICHNormal(_In_ const Poly::Polyhedron& polyhedron, _In_ const int ichIncludePointLimit);

	std::vector<VMACH::Polygon3D>	GenerateVoronoi(_In_ const int cellCount);
	std::vector<VMACH::Polygon3D>	GenerateVoronoi(_In_ const std::vector<Vector3>& cellPointVec);
	std::vector<VMACH::Polygon3D>	GenerateFracturePattern(_In_ const int cellCount, _In_ const double mean);

	CompoundInfo					ApplyFracture(_In_ const CompoundInfo& preResult, 
												  _In_ const std::vector<VMACH::Polygon3D>& voroPolyVec, 
												  _In_ const std::vector<Vector3>& spherePointCloud, 
												  _In_ bool partial = false);

	void							SetExtract(_Inout_ CompoundInfo& preResult);

	void							_MeshIslandLoop(const int index, const Poly::Polyhedron& mesh, std::set<int>& group);
	std::vector<std::set<int>>		CheckMeshIsland(_In_ const Poly::Polyhedron& polyhedron);

	void							HandleConvexIsland(_Inout_ CompoundInfo& compoundInfo);
	void							MergeOutOfImpact(_Inout_ CompoundInfo& compoundInfo, _In_ const std::vector<Vector3>& spherePointCloud);
	void							Refitting(_Inout_ std::vector<Piece>& targetPieceVec);

	// Utility
	bool							ConvexOutOfSphere(_In_ const Poly::Polyhedron& polyhedron,
													  _In_ const std::vector<std::vector<int>>& extract,
													  _In_ const std::vector<Vector3>& spherePointCloud,
													  _In_ const Vector3 origin,
													  _In_ const float radius);

	bool							ConvexRayIntersection(_In_ const VMACH::Polygon3D& convex, 
														  _In_ const Ray ray, 
														  _Out_ float& dist);

	// Helper functions
	void							CreateTextureResource(_In_ const wchar_t* fileName,
														  _Out_ ID3D12Resource** texture,
														  _In_ ID3D12Resource** uploadHeap,
														  _In_ UINT index) const;

	void							LoadModelData(_In_ const std::string fileName,
												  _In_ XMFLOAT3 scale,
												  _In_ XMFLOAT3 translate,
												  _Out_ std::vector<VertexNormalColor>& vertices,
												  _Out_ std::vector<uint32_t>& indices);

	StaticMesh*						PrepareMeshResource(_In_ const std::vector<VertexNormalColor>& vertices, 
														_In_ const std::vector<uint32_t>& indices);

	DynamicMesh*					PrepareDynamicMeshResource(_In_ const std::vector<VertexNormalColor>& vertices,
															   _In_ const std::vector<uint32_t>& indices);

	void							UpdateDynamicMesh(_Inout_ DynamicMesh* dynamicMesh,
													  _In_ const std::vector<VertexNormalColor>& vertices,
													  _In_ const std::vector<uint32_t>& indices);

	// Constants
	static constexpr XMVECTORF32						GRAY					= { 0.15f, 0.15f, 0.15f, 1.0f };
	static constexpr XMVECTORF32						DEFAULT_UP_VECTOR		= { 0.f, 1.f, 0.f, 0.f };
	static constexpr XMVECTORF32						DEFAULT_FORWARD_VECTOR	= { 0.f, 0.f, 1.f, 0.f };
	static constexpr XMVECTORF32						DEFAULT_RIGHT_VECTOR	= { 1.f, 0.f, 0.f, 0.f };
	static constexpr XMFLOAT4X4							IDENTITY_MATRIX			= { 1.f, 0.f, 0.f, 0.f,
																					0.f, 1.f, 0.f, 0.f,
																					0.f, 0.f, 1.f, 0.f,
																					0.f, 0.f, 0.f, 1.f };

	// Input
	std::unordered_map<UINT8, bool>                     m_keyTracker;
	bool												m_isFlightMode;

	// Application state
	HWND                                                m_window;
	int                                                 m_outputWidth;
	int                                                 m_outputHeight;
	float											    m_aspectRatio;
	bool												m_fullScreenMode;
	static constexpr DXGI_FORMAT                        c_backBufferFormat		= DXGI_FORMAT_B8G8R8A8_UNORM;
	static constexpr DXGI_FORMAT                        c_rtvFormat				= DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	static constexpr DXGI_FORMAT                        c_depthBufferFormat		= DXGI_FORMAT_D32_FLOAT;
	static constexpr UINT                               c_swapBufferCount		= 3;
	
	// #TODO : Currently set count as constant.
	static constexpr UINT								c_nSBCnt				= 100;

	// Back buffer index
	UINT                                                m_backBufferIndex;

	// Descriptor sizes
	UINT                                                m_rtvDescriptorSize;
	UINT												m_dsvDescriptorSize;
	UINT												m_cbvSrvDescriptorSize;

	// Options
	D3D_FEATURE_LEVEL                                   m_featureLevel;

	// Device resources
	Microsoft::WRL::ComPtr<IDXGIFactory4>               m_dxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device>                m_d3dDevice;

	// Fence objects
	Microsoft::WRL::ComPtr<ID3D12Fence>                 m_fence;
	UINT64                                              m_fenceValues[c_swapBufferCount];
	Microsoft::WRL::Wrappers::Event                     m_fenceEvent;

	// Command objects
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>          m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>      m_commandAllocators[c_swapBufferCount];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>   m_commandList;

	// Descriptor heaps
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>        m_rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>        m_dsvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>        m_srvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>        m_srvDescriptorHeapSB;

	// Root signature and pipeline state objects
	Microsoft::WRL::ComPtr<ID3D12RootSignature>         m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>         m_opaquePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>         m_noShadowPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>         m_wireframePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>         m_coloredWireframePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>         m_shadowPSO;

	// CB
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_cbOpaqueUploadHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_cbShadowUploadHeap;
	OpaqueCB*											m_cbOpaqueMappedData;
	ShadowCB*											m_cbShadowMappedData;
	D3D12_GPU_VIRTUAL_ADDRESS						    m_cbOpaqueGpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS						    m_cbShadowGpuAddress;

	// Structured Buffer
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_sbUploadHeap;

	// Resources
	Microsoft::WRL::ComPtr<IDXGISwapChain3>             m_swapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_renderTargets[c_swapBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_depthStencil;

	// Viewport and scissor rect
	D3D12_VIEWPORT                                      m_viewport;
	D3D12_RECT                                          m_scissorRect;

	// Textures
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_colorLTexResource;
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_colorRTexResource;
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_heightLTexResource;
	Microsoft::WRL::ComPtr<ID3D12Resource>              m_heightRTexResource;

	// Meshes
	UINT                                                m_modelIndex;
	std::vector<MeshBase*>								m_meshVec;
	std::vector<physx::PxRigidActor*>					m_rigidVec;
	std::vector<MeshSB>									m_meshMatrixVec;
	std::vector<Vector3>								m_spherePointCloud;

	// Shadow
	std::unique_ptr<ShadowMap>  			            m_shadowMap;
	UINT												m_shadowMapSize;
	BoundingSphere										m_sceneBounds;

	// Game state
	DX::StepTimer                                       m_timer;

	// Rendering options
	bool												m_renderShadow;
	bool												m_lightRotation;

	// Feature parameters
	bool                                                m_executeNextStep;
	FractureArgs										m_fractureArgs;
	FractureResult										m_fractureResult;
	FractureStorage										m_fractureStorage;
	std::vector<VMACH::Polygon3D>                       m_convexVec;

	// WVP matrices
	XMMATRIX											m_viewMatrix;
	XMMATRIX											m_projectionMatrix;

	// Camera states
	XMVECTOR											m_camPosition;
	XMVECTOR											m_camLookTarget;
	XMMATRIX											m_camRotationMatrix;
	XMVECTOR											m_camUp;
	XMVECTOR											m_camRight;
	XMVECTOR											m_camForward;
	float                                               m_camYaw;
	float										        m_camPitch;
	bool                                                m_orbitMode;
	float                                               m_camMoveSpeed;
	float										        m_camRotateSpeed;

	// Light states
	XMVECTOR											m_lightDirection;

	// Shadow Map states
	XMFLOAT4X4											m_shadowTransform;
	float                                               m_lightNearZ;
	float                                               m_lightFarZ;
	XMFLOAT3											m_lightPosition;
	XMFLOAT4X4											m_lightView;
	XMFLOAT4X4											m_lightProj;
};
