#ifndef MESH_H
#define MESH_H

struct VertexNormalColor
{
	DirectX::XMFLOAT3	Position;
	DirectX::XMFLOAT3	Normal;
	DirectX::XMFLOAT3   Color;

	explicit VertexNormalColor(
		const DirectX::XMFLOAT3 _position = DirectX::XMFLOAT3(0, 0, 0),
		const DirectX::XMFLOAT3 _normal   = DirectX::XMFLOAT3(0, 0, 0),
		const DirectX::XMFLOAT3 _color    = DirectX::XMFLOAT3(0.25f, 0.25f, 0.25f)) : Position(_position), Normal(_normal), Color(_color) {}
};

struct Mesh
{
public:
	enum RenderOptionType
	{
		NOT_RENDER		= 0x00,
		SOLID			= 0x01,
		WIREFRAME		= 0x02
	};

	Microsoft::WRL::ComPtr<ID3D12Resource>              VB;
	Microsoft::WRL::ComPtr<ID3D12Resource>              IB;
	Microsoft::WRL::ComPtr<ID3D12Resource>				VertexUploadHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource>				IndexUploadHeap;
	D3D12_VERTEX_BUFFER_VIEW                            VBV;
	D3D12_INDEX_BUFFER_VIEW                             IBV;
	std::vector<VertexNormalColor>						VertexData;
	std::vector<uint32_t>								IndexData;
	size_t											    VBSize;
	size_t											    IBSize;
	uint32_t										    VertexCount;
	uint32_t										    IndexCount;
	uint8_t												RenderOption;

	Mesh() : VBV({ 0 }), IBV({ 0 }), VBSize(0), IBSize(0), VertexCount(0), IndexCount(0), RenderOption(SOLID | WIREFRAME) {}
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

#endif