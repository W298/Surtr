#ifndef MESH_H
#define MESH_H

struct VertexNormalColor
{
	DirectX::XMFLOAT3	Position;
	DirectX::XMFLOAT3	Normal;
	DirectX::XMFLOAT3   Color;

	explicit VertexNormalColor(const DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0, 0, 0),
							   const DirectX::XMFLOAT3 normal = DirectX::XMFLOAT3(0, 0, 0),
							   const DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(0.25f, 0.25f, 0.25f)) : Position(position), Normal(normal), Color(color) {}
};

struct MeshBase
{
	enum RenderOptionType
	{
		NOT_RENDER = 0x00,
		SOLID = 0x01,
		WIREFRAME = 0x02
	};

	Microsoft::WRL::ComPtr<ID3D12Resource>              VB;
	Microsoft::WRL::ComPtr<ID3D12Resource>              IB;
	D3D12_VERTEX_BUFFER_VIEW                            VBV;
	D3D12_INDEX_BUFFER_VIEW                             IBV;
	std::vector<VertexNormalColor>						VertexData;
	std::vector<uint32_t>								IndexData;
	uint32_t										    VertexCount;
	uint32_t										    IndexCount;
	uint8_t												RenderOption;

	MeshBase() : VBV({ 0 }), IBV({ 0 }), VertexCount(0), IndexCount(0), RenderOption(SOLID | WIREFRAME) {}
	
	MeshBase(const std::vector<VertexNormalColor>& vertexData, const std::vector<uint32_t>& indexData) : 
		VBV({ 0 }), 
		IBV({ 0 }), 
		VertexData(vertexData), 
		IndexData(indexData), 
		VertexCount(vertexData.size()), 
		IndexCount(indexData.size()), 
		RenderOption(SOLID | WIREFRAME) {}

	~MeshBase()
	{
		VB.Reset();
		IB.Reset();
		VertexData.clear();
		IndexData.clear();
	}

	void Render(ID3D12GraphicsCommandList* commandList, uint32_t meshIndex, uint32_t debug = 0)
	{
		const uint32_t root[] = { meshIndex, debug };

		commandList->SetGraphicsRoot32BitConstants(3, 2, &root, 0);

		commandList->IASetVertexBuffers(0, 1, &VBV);
		commandList->IASetIndexBuffer(&IBV);
		commandList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
	}
};

struct StaticMesh : public MeshBase
{
	Microsoft::WRL::ComPtr<ID3D12Resource>				VertexUploadHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource>				IndexUploadHeap;
	size_t											    VBSize;
	size_t											    IBSize;

	StaticMesh() : MeshBase(), VBSize(0), IBSize(0) {}

	StaticMesh(const std::vector<VertexNormalColor>& vertexData, const std::vector<uint32_t>& indexData) :
		MeshBase(vertexData, indexData),
		VBSize(sizeof(VertexNormalColor) * VertexCount),
		IBSize(sizeof(uint32_t) * IndexCount) {}

	~StaticMesh()
	{
		VertexUploadHeap.Reset();
		IndexUploadHeap.Reset();
	}
};

struct DynamicMesh : public MeshBase
{
	size_t												RenderVBSize;
	size_t												RenderIBSize;
	size_t											    AllocatedVBSize;
	size_t											    AllocatedIBSize;

	DynamicMesh() : MeshBase(), RenderVBSize(0), RenderIBSize(0), AllocatedVBSize(0), AllocatedIBSize(0) {}
	
	DynamicMesh(const std::vector<VertexNormalColor>& vertexData, const std::vector<uint32_t>& indexData) :
		MeshBase(vertexData, indexData),
		RenderVBSize(sizeof(VertexNormalColor) * VertexCount),
		RenderIBSize(sizeof(uint32_t) * IndexCount),
		AllocatedVBSize(RenderVBSize),
		AllocatedIBSize(RenderIBSize) {}

	DynamicMesh(const std::vector<VertexNormalColor>& vertexData, 
				const std::vector<uint32_t>& indexData, 
				const size_t allocatedVBSize, 
				const size_t allocatedIBSize) : 
		MeshBase(vertexData, indexData),
		RenderVBSize(sizeof(VertexNormalColor) * VertexCount),
		RenderIBSize(sizeof(uint32_t) * IndexCount),
		AllocatedVBSize(allocatedVBSize),
		AllocatedIBSize(allocatedIBSize) {}

	DynamicMesh(const std::vector<VertexNormalColor>& vertexData,
				const std::vector<uint32_t>& indexData,
				const uint16_t mutiply) :
		MeshBase(vertexData, indexData),
		RenderVBSize(sizeof(VertexNormalColor) * VertexCount),
		RenderIBSize(sizeof(uint32_t) * IndexCount),
		AllocatedVBSize(RenderVBSize * mutiply),
		AllocatedIBSize(RenderIBSize * mutiply) {}

	void AllocateVB(ID3D12Device* device)
	{
		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(AllocatedVBSize);
		DX::ThrowIfFailed(
			device->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(VB.ReleaseAndGetAddressOf())));

		// Initialize vertex buffer view.
		VBV.BufferLocation = VB->GetGPUVirtualAddress();
		VBV.StrideInBytes = sizeof(VertexNormalColor);
		VBV.SizeInBytes = AllocatedVBSize;
	}

	void AllocateIB(ID3D12Device* device)
	{
		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(AllocatedIBSize);
		DX::ThrowIfFailed(
			device->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(IB.ReleaseAndGetAddressOf())));

		// Initialize index buffer view.
		IBV.BufferLocation = IB->GetGPUVirtualAddress();
		IBV.Format = DXGI_FORMAT_R32_UINT;
		IBV.SizeInBytes = AllocatedIBSize;
	}

	void UploadVB()
	{
		// Upload vertex data.
		CD3DX12_RANGE readRange(0, 0);
		UINT8* vertexDataBegin = nullptr;

		DX::ThrowIfFailed(VB->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)));
		memcpy(vertexDataBegin, VertexData.data(), RenderVBSize);
		VB->Unmap(0, nullptr);
	}

	void UploadIB()
	{
		// Upload index data.
		CD3DX12_RANGE readRange(0, 0);
		UINT8* indexDataBegin = nullptr;

		DX::ThrowIfFailed(IB->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)));
		memcpy(indexDataBegin, IndexData.data(), RenderIBSize);
		IB->Unmap(0, nullptr);
	}

	void Clean()
	{
		VertexData.clear();
		IndexData.clear();
		VertexCount = 0;
		IndexCount = 0;
		
		RenderVBSize = 0;
		RenderIBSize = 0;
	}

	void UpdateMeshData(const std::vector<VertexNormalColor>& vertexData, const std::vector<uint32_t>& indexData)
	{
		VertexData = vertexData;
		IndexData = indexData;
		VertexCount = VertexData.size();
		IndexCount = IndexData.size();

		RenderVBSize = sizeof(VertexNormalColor) * VertexCount;
		RenderIBSize = sizeof(uint32_t) * IndexCount;
	}
};

#endif