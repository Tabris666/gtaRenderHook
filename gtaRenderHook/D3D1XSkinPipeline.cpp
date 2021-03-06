#include "stdafx.h"
#include "D3D1XSkinPipeline.h"
#include "D3DRenderer.h"
#include "D3D1XShader.h"
#include "D3D1XEnumParser.h"
#include "CDebug.h"
#include "RwRenderEngine.h"
#include "D3D1XStateManager.h"

CD3D1XSkinPipeline::CD3D1XSkinPipeline(CD3DRenderer* pRenderer): 
#ifndef DebuggingShaders
	CD3D1XPipeline(pRenderer, "RwSkinTesselation")
#else
	CD3D1XPipeline(pRenderer, L"RwSkinTesselation")
#endif // !DebuggingShaders
{
#ifndef DebuggingShaders
	m_pDS = new CD3D1XShader(m_pRenderer, RwD3D1XShaderType::DS, "shaders/RwSkinTesselation.fx", "DS");
	m_pHS = new CD3D1XShader(m_pRenderer, RwD3D1XShaderType::HS, "shaders/RwSkinTesselation.fx", "HS");
#else
	m_pDS = new CD3D1XShader(m_pRenderer, RwD3D1XShaderType::DS, L"shaders/RwSkinTesselation.fx", "DS");
	m_pHS = new CD3D1XShader(m_pRenderer, RwD3D1XShaderType::HS, L"shaders/RwSkinTesselation.fx", "HS");
#endif // !DebuggingShaders

	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(RwV4d);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	m_pRenderer->getDevice()->CreateBuffer(&bd, nullptr, &m_pMaterialDataBuffer);
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(RwV4d)*3*64;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	m_pRenderer->getDevice()->CreateBuffer(&bd, nullptr, &m_pSkinningDataBuffer);
}


CD3D1XSkinPipeline::~CD3D1XSkinPipeline()
{
	if (m_pDS) delete m_pDS;
	if (m_pHS) delete m_pHS;
	if (m_pMaterialDataBuffer) m_pMaterialDataBuffer->Release();
	if (m_pSkinningDataBuffer) m_pSkinningDataBuffer->Release();
}

bool CD3D1XSkinPipeline::Instance(void *object, RxD3D9ResEntryHeader *resEntryHeader, RwBool reinstance)
{
	RpAtomic* atomic = (RpAtomic*)object;
	RpGeometry* geom = atomic->geometry;
	resEntryHeader->totalNumVertex = geom->numVertices;
	// Create Vertex Declarations and Buffers
	{
		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 12,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, 24,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",		0, DXGI_FORMAT_R8G8B8A8_UNORM,	0, 32,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "WEIGHTS",	0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 36,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BONES",		0, DXGI_FORMAT_R8G8B8A8_UINT,	0, 52,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		UINT numElements = ARRAYSIZE(layout);

		// Create the input layout
		if (FAILED(m_pRenderer->getDevice()->CreateInputLayout(layout, numElements,
			m_pVS->getBlob()->GetBufferPointer(), m_pVS->getBlob()->GetBufferSize(), (ID3D11InputLayout**)&resEntryHeader->vertexDeclaration)))
		{
			g_pDebug->printError("failed to create Input Layout");
			return false;
		}
		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.ByteWidth = static_cast<UINT>(sizeof(simpleVertexSkin)) * resEntryHeader->totalNumVertex;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA InitData;

		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;

		{
			D3D11_MAPPED_SUBRESOURCE mappedResource{};
			RpHAnimHierarchy* atomicHier = AtomicGetHAnimHier(atomic);
			RpSkin* geomskin = GeometryGetSkin(geom);
			simpleVertexSkin* vertexData = new simpleVertexSkin[static_cast<size_t>(resEntryHeader->totalNumVertex)];
			RwUInt8	indexRemap[252];
			memset(indexRemap, 0, sizeof(indexRemap));
			if (geomskin->meshBoneRLECount > 0) {
				g_pDebug->printMsg("Skin has run length encoding bones");
			}
			else {
				if (atomicHier->numNodes > 0) {
					for (RwUInt8 i = 0; i < static_cast<RwUInt8>(atomicHier->numNodes); i++)
						indexRemap[i] = i;
				}
				else {
					g_pDebug->printMsg("Skin has zero bone indices");
				}
			}
			for (size_t i = 0; i < static_cast<size_t>(resEntryHeader->totalNumVertex); i++)
			{
				vertexData[i].pos = geom->morphTarget[0].verts[i];
				if (geom->morphTarget[0].normals)
					vertexData[i].normal = geom->morphTarget[0].normals[i];
				else
					vertexData[i].normal = { 1,1,1 };
				if (geom->texCoords[0])
					vertexData[i].uv = geom->texCoords[0][i];
				else
					vertexData[i].uv = { 0,0 };
				if (geom->preLitLum&&geom->flags&rpGEOMETRYPRELIT)
					vertexData[i].color = geom->preLitLum[i];
				else
					vertexData[i].color = { 255,255,255,255 };
				//weight = RwRGBAReal{ skin->vertexBoneWeights[i].w0,skin->vertexBoneWeights[i].w1,skin->vertexBoneWeights[i].w2 ,skin->vertexBoneWeights[i].w3 };
				//RwRGBAFromRwRGBAReal(&vertexData[i].weights, &weight);
				vertexData[i].weights = geomskin->vertexBoneWeights[i];
				//if (vertexData[i].weights.w0 == 0.0f) {
				//	vertexData[i].weights = { 1.0f,0.0f,0.0f,0.0f };
				//}
				RwUInt8* indicesToRemap = (RwUInt8*)&geomskin->vertexBoneIndices[i];
				RwUInt8* remapedIndices = (RwUInt8*)&vertexData[i].indices;
				remapedIndices[0] = indexRemap[indicesToRemap[0]];
				remapedIndices[1] = indexRemap[indicesToRemap[1]];
				remapedIndices[2] = indexRemap[indicesToRemap[2]];
				remapedIndices[3] = indexRemap[indicesToRemap[3]];
			}
			if (geom->morphTarget[0].normals == nullptr) {
				for (int i = 0; i < geom->numTriangles; i++)
				{
					RwV3d firstvec = {
						vertexData[geom->triangles[i].vertIndex[1]].pos.x - vertexData[geom->triangles[i].vertIndex[0]].pos.x,
						vertexData[geom->triangles[i].vertIndex[1]].pos.y - vertexData[geom->triangles[i].vertIndex[0]].pos.y,
						vertexData[geom->triangles[i].vertIndex[1]].pos.z - vertexData[geom->triangles[i].vertIndex[0]].pos.z
					};
					RwV3d secondvec = {
						vertexData[geom->triangles[i].vertIndex[0]].pos.x - vertexData[geom->triangles[i].vertIndex[2]].pos.x,
						vertexData[geom->triangles[i].vertIndex[0]].pos.y - vertexData[geom->triangles[i].vertIndex[2]].pos.y,
						vertexData[geom->triangles[i].vertIndex[0]].pos.z - vertexData[geom->triangles[i].vertIndex[2]].pos.z
					};
					RwV3d normal = {
						firstvec.y*secondvec.z - firstvec.z*secondvec.y,
						firstvec.z*secondvec.x - firstvec.x*secondvec.z,
						firstvec.x*secondvec.y - firstvec.y*secondvec.x
					};// (firstvec, secondvec);
					RwReal length = sqrt(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
					normal = { normal.x / length,normal.y / length,normal.z / length };

					vertexData[geom->triangles[i].vertIndex[0]].normal = {
						vertexData[geom->triangles[i].vertIndex[0]].normal.x + normal.x,
						vertexData[geom->triangles[i].vertIndex[0]].normal.y + normal.y,
						vertexData[geom->triangles[i].vertIndex[0]].normal.z + normal.z
					};
					vertexData[geom->triangles[i].vertIndex[1]].normal = {
						vertexData[geom->triangles[i].vertIndex[1]].normal.x + normal.x,
						vertexData[geom->triangles[i].vertIndex[1]].normal.y + normal.y,
						vertexData[geom->triangles[i].vertIndex[1]].normal.z + normal.z
					};
					vertexData[geom->triangles[i].vertIndex[2]].normal = {
						vertexData[geom->triangles[i].vertIndex[2]].normal.x + normal.x,
						vertexData[geom->triangles[i].vertIndex[2]].normal.y + normal.y,
						vertexData[geom->triangles[i].vertIndex[2]].normal.z + normal.z
					};
				}
				for (size_t i = 0; i < static_cast<size_t>(resEntryHeader->totalNumVertex); i++)
				{
					RwReal length = sqrt(vertexData[i].normal.x*vertexData[i].normal.x + vertexData[i].normal.y*vertexData[i].normal.y + vertexData[i].normal.z*vertexData[i].normal.z);
					vertexData[i].normal = { vertexData[i].normal.x / length,vertexData[i].normal.y / length,vertexData[i].normal.z / length };
				}
			}

			InitData.pSysMem = vertexData;

			if (FAILED(m_pRenderer->getDevice()->CreateBuffer(&bd, &InitData, (ID3D11Buffer**)&resEntryHeader->vertexStream[0].vertexBuffer)))
				g_pDebug->printError("Failed to create vertex buffer");
			delete[] vertexData;
		}

	}
	return true;
}

void CD3D1XSkinPipeline::Render(RwResEntry * repEntry, void * object, RwUInt8 type, RwUInt32 flags)
{
	RpAtomic* atomic = (RpAtomic*)object;

	rxInstanceData* entryData = (rxInstanceData*)repEntry;
	if (entryData->header.totalNumIndex == 0)
		return;
	ID3D11DeviceContext* devContext = m_pRenderer->getContext();

	// Render shit
	devContext->IASetInputLayout((ID3D11InputLayout*)entryData->header.vertexDeclaration);

	UINT stride = sizeof(simpleVertexSkin);
	UINT offset = 0;
	devContext->IASetVertexBuffers(0, 1, (ID3D11Buffer**)&entryData->header.vertexStream[0].vertexBuffer, &stride, &offset);
	if (!entryData->header.indexBuffer)
		g_pDebug->printMsg("no IB");
	//g_pStateMgr->SetFillMode(D3D11_FILL_WIREFRAME);
	devContext->IASetIndexBuffer((ID3D11Buffer*)entryData->header.indexBuffer, DXGI_FORMAT_R16_UINT, 0);
	auto featureLevel = m_pRenderer->getFeatureLevel();
	if (featureLevel >= D3D_FEATURE_LEVEL_11_0)
		devContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	else
		devContext->IASetPrimitiveTopology(CD3D1XEnumParser::ConvertPrimTopology(entryData->header.primType));
	m_pVS->Set();
	m_pPS->Set();
	m_pDS->Set();
	m_pHS->Set();
	RpSkin* geomSkin;
	geomSkin = GeometryGetSkin(atomic->geometry);
	RpHAnimHierarchy* hier = AtomicGetHAnimHier(atomic);
	_rwD3D9VSSetActiveWorldMatrix(RwFrameGetLTM((RwFrame*)atomic->object.object.parent));
	rpD3D9SkinVertexShaderMatrixUpdate((RwMatrix*)&RpSkinGlobals.alignedMatrixCache[0],atomic, geomSkin);
	
	devContext->UpdateSubresource(m_pSkinningDataBuffer, 0, nullptr, &RpSkinGlobals.alignedMatrixCache[0], 0, 0);
	devContext->VSSetConstantBuffers(3, 1, &m_pSkinningDataBuffer);
	BOOL oldBlendState = g_pStateMgr->GetAlphaBlendEnable();
	RwUInt8 bAlphaEnable = 0;
	for (size_t i = 0; i < static_cast<size_t>(entryData->header.numMeshes); i++)
	{
		bAlphaEnable = 0;
		RwV4d vec{ entryData->models[i].material->color.red / 255.0f,entryData->models[i].material->color.green / 255.0f,entryData->models[i].material->color.blue / 255.0f,entryData->models[i].material->color.alpha / 255.0f };
		bAlphaEnable |= entryData->models[i].material->color.alpha!=255 || entryData->models[i].vertexAlpha;

		if (entryData->models[i].material->texture) 
		{
			//bAlphaEnable |= GetD3D1XRaster(entryData->models[i].material->texture->raster)->alpha;
			g_pRwCustomEngine->SetTexture(entryData->models[i].material->texture, 0);
		}
		g_pStateMgr->SetAlphaBlendEnable(bAlphaEnable!=0);
		devContext->DrawIndexed(entryData->models[i].numIndex, entryData->models[i].startIndex, entryData->models[i].minVert);
	}
	g_pStateMgr->SetAlphaBlendEnable(oldBlendState);
	m_pDS->ReSet();
	m_pHS->ReSet();
	//g_pStateMgr->SetFillMode(D3D11_FILL_SOLID);
}
