#pragma once

struct Mesh
{
public:
	Microsoft::WRL::ComPtr<ID3D12Resource>              VB;
	Microsoft::WRL::ComPtr<ID3D12Resource>              IB;
	D3D12_VERTEX_BUFFER_VIEW                            VBV;
	D3D12_INDEX_BUFFER_VIEW                             IBV;
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
	}

	void Render(ID3D12GraphicsCommandList* commandList)
	{
		commandList->IASetVertexBuffers(0, 1, &VBV);
		commandList->IASetIndexBuffer(&IBV);
		commandList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
	}
};