#pragma once

struct VertexNormalTex
{
	DirectX::XMFLOAT3	position;
	DirectX::XMFLOAT3	normal;
	DirectX::XMFLOAT2   texCoord;

	explicit VertexNormalTex(
		const DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0, 0, 0),
		const DirectX::XMFLOAT3 normal   = DirectX::XMFLOAT3(0, 0, 0),
		const DirectX::XMFLOAT2 texCoord = DirectX::XMFLOAT2(0, 0)) : position(position), normal(normal), texCoord(texCoord) {}
};

struct Mesh
{
public:
	Microsoft::WRL::ComPtr<ID3D12Resource>              VB;
	Microsoft::WRL::ComPtr<ID3D12Resource>              IB;
	Microsoft::WRL::ComPtr<ID3D12Resource>				VertexUploadHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource>				IndexUploadHeap;
	D3D12_VERTEX_BUFFER_VIEW                            VBV;
	D3D12_INDEX_BUFFER_VIEW                             IBV;
	std::vector<VertexNormalTex>						VertexData;
	std::vector<uint32_t>								IndexData;
	size_t											    VBSize;
	size_t											    IBSize;
	uint32_t										    VertexCount;
	uint32_t										    IndexCount;
	bool												RenderSolid;

	Mesh() : VBV({ 0 }), IBV({ 0 }), VBSize(0), IBSize(0), VertexCount(0), IndexCount(0), RenderSolid(true) {}
	~Mesh()
	{
		VB.Reset();
		IB.Reset();
		VertexUploadHeap.Reset();
		IndexUploadHeap.Reset();
		VertexData.clear();
		IndexData.clear();
	}

	void Render(ID3D12GraphicsCommandList* commandList)
	{
		commandList->IASetVertexBuffers(0, 1, &VBV);
		commandList->IASetIndexBuffer(&IBV);
		commandList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
	}
};