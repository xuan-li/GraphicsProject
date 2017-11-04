#include "BFF.h"

BFFSolver::BFFSolver(SurfaceMesh & mesh)
	:mesh_(mesh), slicer_(mesh)
{

}

SurfaceMesh BFFSolver::Compute()
{
	Init();
	
	ComputeConformalFactors();

	Slice();

	BoundaryUToTargetK();

	IntegrateBoundaryCurve();

	ExtendToInterior();

	NormalizeUV();

	return sliced_mesh_;
}

void BFFSolver::Init()
{
	using namespace OpenMesh;
	slicer_.FindAndMarkCutGraphSphere();
	slice_path_ = slicer_.GetLongestPath();
	if (!on_slice_.is_valid()) {
		mesh_.add_property(on_slice_);
	}
	for (auto viter = mesh_.vertices_begin(); viter != mesh_.vertices_end(); ++viter) {
		mesh_.property(on_slice_, *viter) = false;
	}
	for (auto it = slice_path_.begin(); it != slice_path_.end(); ++it) {
		mesh_.property(on_slice_, *it) = true;
	}
	InitType1();
}

void BFFSolver::InitType1()
{
	using namespace OpenMesh;
	SurfaceMesh &mesh = mesh_;
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		mesh.data(v).set_target_curvature(0);
		mesh.data(v).set_singularity(false);
	}
	mesh.data(slice_path_[0]).set_target_curvature(3 * PI / 2);
	mesh.data(slice_path_[0]).set_singularity(true);
	mesh.data(slice_path_[0]).set_order(4);
	mesh.data(slice_path_[slice_path_.size() / 2]).set_target_curvature(PI);
	mesh.data(slice_path_[slice_path_.size() / 2]).set_singularity(true);
	mesh.data(slice_path_[slice_path_.size() / 2]).set_order(2);
	mesh.data(slice_path_.back()).set_target_curvature(3 * PI / 2);
	mesh.data(slice_path_.back()).set_singularity(true);
	mesh.data(slice_path_.back()).set_order(4);
	
}

void BFFSolver::Slice()
{
	using namespace OpenMesh;
	slicer_.ConstructWedge();
	slicer_.SliceAccordingToWedge(sliced_mesh_);
	for (auto viter = mesh_.vertices_begin(); viter != mesh_.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		auto equiv_vts = slicer_.SplitTo(v);
		for (auto it = equiv_vts.begin(); it != equiv_vts.end(); ++it) {
			VertexHandle nv = *it;
			double u = mesh_.data(v).u();
			sliced_mesh_.data(nv).set_u(u);
			sliced_mesh_.data(nv).set_singularity(mesh_.data(v).is_singularity());
		}
	}

	sliced_mesh_.RequestBoundary();
	auto boundary = sliced_mesh_.GetBoundaries().front();
	//std::list<HalfedgeHandle> boundary_list(boundary.begin(), boundary.end());
	for (auto it = boundary.begin(); it != boundary.end(); ++it) {
		VertexHandle v = sliced_mesh_.to_vertex_handle(*it);
		if (sliced_mesh_.data(v).is_singularity())
			cone_vts_.push_back(v);
	}

}

double BFFSolver::CosineLaw(double a, double b, double c)
{
	double cs = (a * a + b * b - c * c) / (2 * a * b);
	assert(-1 <= cs && cs <= 1);
	return acos(cs);
}

void BFFSolver::ComputeCornerAngles(SurfaceMesh &mesh)
{
	using namespace OpenMesh;
	for (auto fiter = mesh.faces_begin(); fiter != mesh.faces_end(); ++fiter) {
		FaceHandle f = *fiter;
		std::vector<HalfedgeHandle> he;

		for (auto fhiter = mesh.fh_iter(f); fhiter.is_valid(); ++fhiter) {
			he.push_back(*fhiter);
		}
		double l[3];
		l[0] = mesh.calc_edge_length(mesh.edge_handle(he[0]));
		l[1] = mesh.calc_edge_length(mesh.edge_handle(he[1]));
		l[2] = mesh.calc_edge_length(mesh.edge_handle(he[2]));
		for (int i = 0; i < 3; ++i) {
			double cs = CosineLaw(l[i], l[(i + 1) % 3], l[(i + 2) % 3]);
			mesh.data(he[i]).set_angle(cs);
		}
	}
}

void BFFSolver::ComputeVertexCurvatures(SurfaceMesh &mesh)
{
	using namespace OpenMesh;
	ComputeCornerAngles(mesh);
	double sum = 0.0;
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		double angle_sum = 0.0;
		for (auto vihiter = mesh.vih_iter(v); vihiter.is_valid(); ++vihiter) {
			HalfedgeHandle h = *vihiter;
			if (mesh.is_boundary(h)) continue;
			angle_sum += mesh.data(h).angle();
		}
		if(!mesh.is_boundary(v))
			mesh.data(v).set_curvature(2 * PI - angle_sum);
		else
			mesh.data(v).set_curvature(PI - angle_sum);
		sum += mesh.data(v).curvature();
	}
	std::cout << "Total Curvature: " << sum/ PI << " pi."<<  std::endl;
}

void BFFSolver::ComputeLaplacian(SurfaceMesh & mesh, bool mode)
{
	using namespace OpenMesh;
	using namespace Eigen;
	
	ReindexVertices(mesh);
	ComputeHalfedgeWeights(mesh);

	Delta_.resize(mesh.n_vertices(), mesh.n_vertices());
	Delta_.setZero();
	std::vector<Eigen::Triplet<double>> A_coefficients;
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		if (mode && v.idx() == 0){
			for (int i = 0; i < mesh.n_vertices(); ++i) {
				A_coefficients.push_back(Eigen::Triplet<double>(mesh.data(v).reindex(), i, 1.));
				continue;
			}
		}
		double s_w = 0;
		for (SurfaceMesh::VertexVertexIter vviter = mesh.vv_iter(v); vviter.is_valid(); ++vviter) {
			VertexHandle neighbor = *vviter;
			HalfedgeHandle h = mesh.find_halfedge(v, neighbor);
			double n_w = mesh.data(h).weight();
			//if (mesh.data(v).is_singularity())
				//n_w *= mesh.data(v).order();
			s_w += n_w;
			A_coefficients.push_back(Eigen::Triplet<double>(mesh.data(v).reindex(), mesh.data(neighbor).reindex(), -n_w));
		}
		A_coefficients.push_back(Eigen::Triplet<double>(mesh.data(v).reindex(), mesh.data(v).reindex(), s_w));
	}
	Delta_.setFromTriplets(A_coefficients.begin(), A_coefficients.end());
}

void BFFSolver::ComputeConformalFactors()
{
	using namespace Eigen;
	SurfaceMesh &mesh = mesh_;
	using namespace OpenMesh;
	
	
	// Using Cherrier Formula
	
	// Construct Sparse system;
	ComputeLaplacian(mesh, true);
	ComputeVertexCurvatures(mesh_);
	Eigen::VectorXd b(mesh.n_vertices());
		
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		b(mesh.data(v).reindex()) = mesh.data(v).curvature() - mesh.data(v).target_curvature();
	}
	b(mesh.data(*mesh.vertices_begin()).reindex()) = 0;
	//b(mesh.n_vertices()) = 0;
	
	
	// add a constraint that u_0 = 0; 
	/*SparseMatrix<double> M(Delta_.rows() + 1, Delta_.cols());
	M.setZero();
	std::vector<Triplet<double>> M_coefficients;
	for (int k = 0; k < Delta_.outerSize(); ++k)
	{
		for (SparseMatrix<double>::InnerIterator it(Delta_, k); it; ++it)
		{
			M_coefficients.push_back(Triplet<double>(it.row(), it.col(), it.value())); 
		}
	}
	for (int i = 0; i < 1; ++i) {
		M_coefficients.push_back(Triplet<double>(Delta_.rows(), i, 1.));
	}
	M.setFromTriplets(M_coefficients.begin(), M_coefficients.end());*/

	SparseLU<SparseMatrix<double>, COLAMDOrdering<int>> solver;
	solver.compute(Delta_);
	if (solver.info() != Eigen::Success)
	{
		std::cerr << "Waring: Eigen decomposition failed" << std::endl;
	}
	Eigen::VectorXd u = solver.solve(b);
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		mesh.data(v).set_u(u(mesh.data(v).reindex()));
	}
	

}

void BFFSolver::ComputeHalfedgeWeights(SurfaceMesh &mesh)
{
	using namespace OpenMesh;
	ComputeCornerAngles(mesh);
	for (auto eiter = mesh.edges_begin(); eiter != mesh.edges_end(); ++eiter) {
		EdgeHandle e = *eiter;
		HalfedgeHandle h0 = mesh.halfedge_handle(e, 0);
		HalfedgeHandle h1 = mesh.halfedge_handle(e, 1);
		HalfedgeHandle h0_next;
		if (!mesh.is_boundary(h0))
			h0_next = mesh.next_halfedge_handle(h0);
		HalfedgeHandle h1_next;
		if (!mesh.is_boundary(h1))
			h1_next = mesh.next_halfedge_handle(h1);

		double weight = 0.;

		if (h0_next.is_valid())
			weight += 1. / tan(mesh.data(h0_next).angle());
		if (h1_next.is_valid())
			weight += 1. / tan(mesh.data(h1_next).angle());
		weight *= 0.5;
		//if (weight < 0)
			//weight = 0.01;
		mesh.data(h0).set_weight(weight);
		mesh.data(h1).set_weight(weight);
	}
}

void BFFSolver::ReindexVertices(SurfaceMesh & mesh)
{
	using namespace OpenMesh;
	int n_interior = 0;
	int n_boundary = 0;

	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		if (!mesh.is_boundary(v)) {
			mesh.data(v).set_reindex(n_interior);
			++n_interior;
		}
	}

	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		if (mesh.is_boundary(v)) {
			mesh.data(v).set_reindex(n_interior + n_boundary);
			++n_boundary;
		}
	}

	n_boundary_ = n_boundary;
	n_interior_ = n_interior;
}

void BFFSolver::BoundaryUToTargetK()
{
	using namespace Eigen;
	using namespace OpenMesh;
	SurfaceMesh &mesh = sliced_mesh_;
	
	ComputeLaplacian(mesh);
	ComputeVertexCurvatures(mesh);
	VectorXd omega(mesh.n_vertices());
	VectorXd u(mesh.n_vertices());
	VectorXd k(n_boundary_);

	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		if (!mesh.is_boundary(v)) {
			omega(mesh.data(v).reindex()) = mesh.data(v).curvature();
			u(mesh.data(v).reindex()) = mesh.data(v).u();
		}
		else {
			omega(mesh.data(v).reindex()) = 0.;
			u(mesh.data(v).reindex()) = mesh.data(v).u();
			k(mesh.data(v).reindex() - n_interior_) = mesh.data(v).curvature();
		}
	}

	double target_sum = 2 * PI - k.sum();

	SparseLU<SparseMatrix<double>> solver;
	solver.compute(Delta_.block(0,0,n_interior_, n_interior_));
	if (solver.info() != Eigen::Success)
	{
		std::cerr << "Waring: Eigen decomposition failed" << std::endl;
	}
	//Eigen::SparseMatrix<double> A_IB = Delta_.block(0, n_interior_, n_interior_, n_boundary_);
	//Eigen::SparseMatrix<double> A_BB = Delta_.block(n_interior_, n_interior_, n_boundary_, n_boundary_);
	//Eigen::SparseMatrix<double> A_BI = Delta_.block(n_interior_, 0, n_boundary_, n_interior_);
	//VectorXd u_B = u.segment(n_interior_, n_boundary_);
	//std::cout << u_B << std::endl;
	//VectorXd omega_I = omega.segment(0, n_interior_);
	//Eigen::VectorXd to_inverse1 = omega_I - A_IB * u_B;
	//Eigen::VectorXd inverse1 = solver.solve(to_inverse1);
	//Eigen::VectorXd B = A_BI * inverse1 - A_BB * u_B;
	//
	//Eigen::VectorXd interse2 = solver.solve(A_IB * VectorXd::Constant(n_boundary_, 1.));
	//Eigen::VectorXd X = -A_BI * interse2 +  A_BB * VectorXd::Constant(n_boundary_, 1.);

	//// solve factor * sum(X) + sum(B) = target_sum;
	//

	Eigen::VectorXd h = -(Delta_ * u).block(n_interior_,0, n_boundary_, 1);
	//std::cout << X.sum() << "\t" << B.sum() << "\t" << h.sum() - target_sum << "\t"  << std::endl;;

	//h = h / h.sum() * (k.sum() - 2 * PI);
	Eigen::VectorXd target_k = k + h;
	
	auto boundary = sliced_mesh_.GetBoundaries().front();
	//std::list<HalfedgeHandle> boundary_list(boundary.begin(), boundary.end());
	for (auto it = boundary.begin(); it != boundary.end(); ++it) {
		VertexHandle v = sliced_mesh_.to_vertex_handle(*it);
		mesh.data(v).set_target_curvature(target_k(mesh.data(v).reindex() - n_interior_));
	}

	std::cout << "Singularities' curvature:" << std::endl;
	for (auto it = cone_vts_.begin(); it != cone_vts_.end(); ++it) {
		VertexHandle v = *it;
		std::cout << target_k(mesh.data(v).reindex() - n_interior_) << std::endl;
	}
}

void BFFSolver::IntegrateBoundaryCurve()
{
	using namespace OpenMesh;
	using namespace Eigen;
	SurfaceMesh &mesh = sliced_mesh_;

	VPropHandleT<double> cumulative_angle;
	VPropHandleT<Vec2d> tangent;
	mesh.add_property(cumulative_angle);
	mesh.add_property(tangent);

	auto boundary = mesh.GetBoundaries().front();

	// Set edge length and Construct tanget matrix and normalization matrix;

	Eigen::MatrixXd T(2, n_boundary_);
	Eigen::MatrixXd N_inverse(n_boundary_, n_boundary_);
	T.setZero();
	N_inverse.setZero();

	Eigen::VectorXd L_star(n_boundary_);

	mesh.property(cumulative_angle, mesh.from_vertex_handle(boundary.front())) = 0;
	int i = 0;
	for (auto it = boundary.begin(); it != boundary.end(); ++it, ++i) {
		VertexHandle v0 = mesh.from_vertex_handle(*it);
		VertexHandle v1 = mesh.to_vertex_handle(*it);
		EdgeHandle e = mesh.edge_handle(*it);
		double u0 = mesh.data(v0).u();
		double u1 = mesh.data(v1).u();
		double l = mesh.calc_edge_length(e);
		double l_star = exp(0.5 * (u0 + u1)) * l;
		double angle = mesh.property(cumulative_angle, v0);
		mesh.property(cumulative_angle, v1) = angle + mesh.data(v0).target_curvature();
		mesh.property(tangent, v0) = Vec2d(cos(angle), sin(angle));

		T(0,i) = cos(angle);
		T(1, i) = sin(angle);

		N_inverse(i, i) = l;
		L_star(i) = l_star;

	}
	
	Eigen::VectorXd L_normalized = L_star - N_inverse * T.transpose() * (T * N_inverse* T.transpose()).inverse() * T * L_star;
	
	i = 0;
	mesh.set_texcoord2D(mesh.from_vertex_handle(boundary.front()), Vec2d(0, 0));
	for (auto it = boundary.begin(); it != boundary.end(); ++it, ++i) {
		VertexHandle v0 = mesh.from_vertex_handle(*it);
		VertexHandle v1 = mesh.to_vertex_handle(*it);
		Vec2d coord = mesh.texcoord2D(v0) + mesh.property(tangent, v0) * L_normalized(i);
		mesh.set_texcoord2D(v1, coord);
	}
	
}

void BFFSolver::ExtendToInterior()
{
	using namespace Eigen;
	using namespace OpenMesh;
	SurfaceMesh &mesh = sliced_mesh_;
	ComputeHarmonicMatrix();
	
	Eigen::MatrixXd b(mesh.n_vertices(), 2);
	b.setZero();

	auto boundary = mesh.GetBoundaries().front();
	for (auto it = boundary.begin(); it != boundary.end(); ++it) {
		VertexHandle v = mesh.from_vertex_handle(*it);
		Vec2d coord = mesh.texcoord2D(v);
		b(v.idx(), 0) = coord[0];
		b(v.idx(), 1) = coord[1];
	}

	SparseLU<SparseMatrix<double>> solver;
	solver.compute(Delta_);
	if (solver.info() != Eigen::Success)
	{
		std::cerr << "Waring: Eigen decomposition failed" << std::endl;
	}
	Eigen::MatrixXd uv = solver.solve(b);
	
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle  v = *viter;
		mesh.set_texcoord2D(v, Vec2d(uv(v.idx(), 0), uv(v.idx(), 1)));
	}
}

void BFFSolver::ComputeHarmonicMatrix()
{
	using namespace OpenMesh;
	using namespace Eigen;
	SurfaceMesh &mesh = sliced_mesh_;

	Delta_.resize(mesh.n_vertices(), mesh.n_vertices());
	Delta_.setZero();
	std::vector<Eigen::Triplet<double>> A_coefficients;
	for (auto viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		if (mesh.is_boundary(v)) {
			A_coefficients.push_back(Eigen::Triplet<double>(v.idx(), v.idx(), 1.));
			continue;
		}
		double s_w = 0;
		for (SurfaceMesh::VertexVertexIter vviter = mesh.vv_iter(v); vviter.is_valid(); ++vviter) {
			VertexHandle neighbor = *vviter;
			HalfedgeHandle h = mesh.find_halfedge(v, neighbor);
			double n_w = mesh.data(h).weight();
			s_w += n_w;
			A_coefficients.push_back(Eigen::Triplet<double>(v.idx(), neighbor.idx(), -n_w));
		}
		A_coefficients.push_back(Eigen::Triplet<double>(v.idx(), v.idx(), s_w));
	}
	Delta_.setFromTriplets(A_coefficients.begin(), A_coefficients.end());
}

void BFFSolver::NormalizeUV()
{
	using namespace OpenMesh;
	SurfaceMesh &mesh = sliced_mesh_;
	Vec2d s(0, 0);
	for (SurfaceMesh::VertexIter viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		s += mesh.texcoord2D(v);
	}
	s /= mesh.n_vertices();
	for (SurfaceMesh::VertexIter viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		mesh.set_texcoord2D(v, mesh.texcoord2D(v) -s);
	}

	double scale = 0;
	for (SurfaceMesh::VertexIter viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		scale = mesh.texcoord2D(v).norm() > scale ? mesh.texcoord2D(v).norm() : scale;
	}
	for (SurfaceMesh::VertexIter viter = mesh.vertices_begin(); viter != mesh.vertices_end(); ++viter) {
		VertexHandle v = *viter;
		mesh.set_texcoord2D(v, mesh.texcoord2D(v) / scale);
	}
}
