#include "GUIViewer.h"

OTEViewer::OTEViewer()
{
}

void OTEViewer::Init()
{
	InitMenu();
	InitKeyboard();
}


void OTEViewer::InitMenu()
{
	callback_init = [this](igl::viewer::Viewer& viewer)
	{
		viewer.ngui->addWindow(Eigen::Vector2i(220, 0), "Control Panel");

		viewer.ngui->addGroup("Mesh IO");
		viewer.ngui->addButton("Load Mesh", [this]() {this->LoadMesh(); });
		viewer.ngui->addButton("Load Texture", [this]() {this->LoadTexture(); });
		viewer.ngui->addButton("Save Mesh", [this]() {this->SaveMesh(); });

		viewer.ngui->addGroup("Core Functions");
		viewer.ngui->addButton("Euclidean Orbifold", [this]() {
			EuclideanOrbifoldSolver solver(this->mesh_);
			this->sliced_mesh_ = solver.Compute();
			this->cone_vts_ = solver.ConeVertices();
			hyperbolic_ = false;
			euclidean_ = true;
		});
		viewer.ngui->addButton("Hyperbolic Orbifold", [this]() {
			HyperbolicOrbifoldSolver solver(this->mesh_);
			this->sliced_mesh_ = solver.Compute();
			this->cone_vts_ = solver.ConeVertices();
			euclidean_ = false;
			hyperbolic_ = true;
		});
		viewer.ngui->addButton("BFF", [this]() {
			BFFSolver solver(this->mesh_);
			this->sliced_mesh_ = solver.Compute();
			this->cone_vts_ = solver.ConeVertices();
			euclidean_ = true;
			hyperbolic_ = false;
		});

		viewer.ngui->addGroup("Viewer Options");


		viewer.ngui->addVariable<ShowOption>(
			"Show Option",
			[this](const ShowOption & v) {  this->show_option_ = v; this->UpdateMeshViewer(); },
			[this]()->ShowOption { return this->show_option_; }
		)->setItems({ "Original", "Sliced", "Embedding", "Covering Space" });
		
		viewer.ngui->addVariable<bool>(
			"Show Boundaries",
			[this](const bool &v) {this->show_boundaries_ = v; this->UpdateMeshViewer(); },
			[this]() -> bool {return this->show_boundaries_; }
		);


		viewer.screen->performLayout();
		return false; 
	};

	
}

void OTEViewer::InitKeyboard()
{
	callback_key_down = [](igl::viewer::Viewer& viewer, unsigned char key, int modifier) 
	{
		return false;
	};
}

void OTEViewer::LoadMesh()
{
	std::string fname = igl::file_dialog_open();
	if (fname.length() == 0)
		return;
	OpenMesh::IO::Options opt;
	opt += OpenMesh::IO::Options::VertexTexCoord;
	OpenMesh::IO::read_mesh(mesh_, fname, opt);
	NormalizeMesh(mesh_);
	UpdateMeshData(mesh_);
	UpdateTextureCoordData(mesh_);

}

void OTEViewer::LoadTexture()
{
	std::string fname = igl::file_dialog_open();
	if (fname.length() == 0)
		return;
	igl::png::texture_from_png(fname, R_, G_, B_, A_);
	data.set_texture(R_, G_, B_, A_);
}

void OTEViewer::SaveMesh()
{
	std::string fname = igl::file_dialog_save();
	if (fname.length() == 0)
		return;
	OpenMesh::IO::Options opt;
	opt += OpenMesh::IO::Options::VertexTexCoord;

	if (show_option_ == ORIGINAL) {
		OpenMesh::IO::write_mesh(mesh_, fname, opt);
	}

	else if (show_option_ == SLICED) {
		OpenMesh::IO::write_mesh(sliced_mesh_, fname, opt);
	}


	
}

void OTEViewer::UpdateMeshData(SurfaceMesh &mesh)
{
	OpenMeshToMatrix(mesh, V_,V_normal_, F_, F_normal_);
	TC_.resize(F_.rows(), 3);
	TC_.col(0) = Eigen::VectorXd::Constant(TC_.rows(), 1.0);
	TC_.col(1) = Eigen::VectorXd::Constant(TC_.rows(), 1.0);
	TC_.col(2) = Eigen::VectorXd::Constant(TC_.rows(), 1.0);
	if (V_.rows() == 0) return;
	data.clear();
	data.set_mesh(V_, F_);
	data.set_colors(TC_);
	data.set_normals(V_normal_);
	data.set_normals(F_normal_);
}


void OTEViewer::UpdateTextureCoordData(SurfaceMesh &mesh)
{
	OpenMeshCoordToMatrix(mesh, UV_);
	data.set_uv(UV_);
	if (R_.rows() > 0)
		data.set_texture(R_, G_, B_);
}


void OTEViewer::ShowUV()
{
	if (UV_.rows() == 0) return;
	UV_Z0_.resize(UV_.rows(), 3);
	UV_Z0_.setZero();
	UV_Z0_.block(0, 0, UV_.rows(), UV_.cols()) = UV_;
	data.clear();
	data.set_mesh(UV_Z0_, F_);
	data.set_uv(UV_);
	data.set_colors(TC_);
	data.set_texture(R_, G_, B_);

	Eigen::MatrixXd V_normal = V_normal_;
	V_normal.col(2) = (V_normal.col(2)).cwiseAbs();
	Eigen::MatrixXd F_normal = F_normal_;
	F_normal.col(2) = F_normal.col(2).cwiseAbs();
	data.set_normals(V_normal);
	data.set_normals(F_normal);
}

void OTEViewer::ShowCoveringSpace()
{
	if (euclidean_) {
		EuclideanCoveringSpaceComputer covering_computer(sliced_mesh_, cone_vts_);
		covering_computer.Compute();
		covering_computer.GenerateMeshMatrix(V_, V_normal_, F_, F_normal_);
		data.clear();
		data.set_mesh(V_, F_);
		V_normal_.col(2) = (V_normal_.col(2)).cwiseAbs();
		F_normal_.col(2) = (F_normal_.col(2)).cwiseAbs();
		data.set_normals(V_normal_);
		data.set_normals(F_normal_);
		data.set_uv(V_.block(0, 0, V_.rows(),2));
	}

	if (hyperbolic_) {
		HyperbolicCoveringSpaceComputer covering_computer(sliced_mesh_, cone_vts_);
		covering_computer.Compute();
		covering_computer.GenerateMeshMatrix(V_, V_normal_, F_, F_normal_);
		data.clear();
		data.set_mesh(V_, F_);
		V_normal_.col(2) = (V_normal_.col(2)).cwiseAbs();
		F_normal_.col(2) = (F_normal_.col(2)).cwiseAbs();
		data.set_normals(V_normal_);
		data.set_normals(F_normal_);
		data.set_uv(V_.block(0, 0, V_.rows(), 2));
	}

}

void OTEViewer::ShowHalfedges(SurfaceMesh &mesh, std::vector<OpenMesh::HalfedgeHandle> h_vector)
{
	// This algorithm to generate cylinder is from the library libhedra
	using namespace OpenMesh;

	if (h_vector.size() == 0) return;

	Eigen::MatrixXd lineV, lineTC, bigV, bigTC;
	Eigen::MatrixXi lineT, bigT;
	Eigen::MatrixXd P1, P2;
	Eigen::MatrixXd lineColors;
	double radius = 0.005;

	HalfedgesToMatrix(mesh, h_vector, P1, P2);
	lineColors.resize(P1.rows(), 3);
	lineColors.setZero();
	lineColors.col(0) = Eigen::VectorXd::Constant(P1.rows(), 1);
	LineCylinders(
		P1,
		P2,
		radius, lineColors,
		10,
		false,
		lineV,
		lineT,
		lineTC);

	MergeMeshMatrix(V_, F_, TC_, lineV, lineT, lineTC, bigV, bigT, bigTC);

	
	data.clear();
	data.set_mesh(bigV, bigT);
	data.set_colors(bigTC);
}

void OTEViewer::ShowBoundaries(SurfaceMesh &mesh)
{
	mesh.RequestBoundary();
	auto boundaries = mesh.GetBoundaries();
	ShowHalfedges(mesh,boundaries.front());
	
}

void OTEViewer::UpdateMeshViewer()
{
	if (show_option_ == ORIGINAL) {
		UpdateMeshData(mesh_);
		UpdateTextureCoordData(mesh_);
	}
	else if (show_option_ == SLICED) {
		UpdateMeshData(sliced_mesh_);
		UpdateTextureCoordData(sliced_mesh_);
		if (show_boundaries_) {
			ShowBoundaries(sliced_mesh_);
		}
	}
	else if (show_option_ == EMBEDDING) {
		UpdateMeshData(sliced_mesh_);
		UpdateTextureCoordData(sliced_mesh_);
		ShowUV();
	}

	else if (show_option_ == COVERING_SPACE) {
		ShowCoveringSpace();
	}

}
