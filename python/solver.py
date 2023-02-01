import abc
import numpy as np
from matplotlib import pyplot as plt
from scipy import optimize
from sys import argv

import concept
import equation
import riemann
import spatial
import temporal


class SolverBase(abc.ABC):

    def plot(self, t_curr, n_point: int):
        points = np.linspace(self._spatial.x_left(), self._spatial.x_right(), n_point)
        expect_solution = np.ndarray(n_point)
        approx_solution = np.ndarray(n_point)
        for i in range(n_point):
            point_i = points[i]
            approx_solution[i] = self._spatial.get_solution_value(point_i)
            expect_solution[i] = self.u_exact(point_i, t_curr)
        plt.figure(figsize=(6, 3))
        plt.plot(points, approx_solution, 'b+', label='Approximate Solution')
        plt.plot(points, expect_solution, 'r-', label='Exact Solution')
        plt.ylim([-1.4, 1.4])
        plt.title(f't = {t_curr:.2f}')
        plt.legend()
        plt.xticks(np.linspace(self._spatial.x_left(), self._spatial.x_right(),
            self._spatial.n_element() + 1))
        plt.grid()
        # plt.show()
        plt.savefig(f't={t_curr:.2f}.pdf')

    @abc.abstractmethod
    def a_max(self):
        pass

    def run(self, t_start: float, t_stop: float,  n_step: int):
        delta_t = (t_stop - t_start) / n_step
        cfl = self.a_max() * delta_t / self._spatial.delta_x()
        print(f"n_step = {n_step}, delta_t = {delta_t}, cfl = {cfl}")
        self._spatial.initialize(lambda x_global: self.u_init(x_global))
        def plot(t_curr):
            self.plot(t_curr, n_point=101)
        self._ode_solver.solve(self._spatial, plot, t_start, t_stop, delta_t)


class LinearAdvection(SolverBase):

    def __init__(self, a_const: float,
            spatial_scheme: concept.SpatialScheme,
            ode_solver: concept.OdeSolver) -> None:
        self._a_const = a_const
        self._spatial = spatial_scheme
        self._ode_solver = ode_solver

    def a_max(self):
        return np.abs(self._a_const)

    def u_init(self, x_global):
        x_global = x_global - self._spatial.x_left()
        value = np.sin(x_global * np.pi * 2 / self._spatial.length())
        return value  # np.sign(value)

    def u_exact(self, x_global, t_curr):
        return self.u_init(x_global - self._a_const * t_curr)


class InviscidBurgers(SolverBase):

    def __init__(self, k: float,
            spatial_scheme: concept.SpatialScheme,
            ode_solver: concept.OdeSolver) -> None:
        self._k = k
        self._spatial = spatial_scheme
        self._ode_solver = ode_solver
        self._u_prev = 0.0
        self._x_mid = (self._spatial.x_left() + self._spatial.x_right()) / 2

    def a_max(self):
        return self._k

    def u_init(self, x_global):
        x_global = x_global - self._spatial.x_left()
        value = np.sin(x_global * np.pi * 2 / self._spatial.length())
        return value

    def u_exact(self, x_global, t_curr):
        # Solve u from u_curr = u_init(x - a(u_curr) * t_curr).
        def func(u_curr):
            return u_curr - self.u_init(x_global - self._k * u_curr * t_curr)
        if np.abs(x_global - self._x_mid) < 1e-6:
            root = 0.0
        else:
            if x_global > self._x_mid and self._u_prev > 0:
                self._u_prev = -self._u_prev
            a = self._u_prev - 0.2
            b = self._u_prev + 0.2
            # print(x_global, self._x_mid, self._u_prev, func(a), func(b))
            root = optimize.bisect(func, a, b)
            self._u_prev = root
        return root


if __name__ == '__main__':
    if len(argv) < 11:
        print("Usage: \n  python3 solver.py <degree> <n_element> <x_left>",
            "<x_right> <rk_order> <t_start> <t_stop> <delta_t>",
            "<method> <problem>\nin which, <method> in (DG, FR, DGFR),",
            "<problem> in (Linear, Burgers).")
        exit(-1)
    method = argv[9]
    if method == 'DG':
        spatial_class = spatial.LagrangeDG
    elif method == 'FR':
        spatial_class = spatial.LagrangeFR
    elif method == 'DGFR':
        spatial_class = spatial.DGwithLagrangeFR
    else:
        assert False
    problem = argv[10]
    if problem == 'Linear':
        solver_class = LinearAdvection
        equation_class = equation.LinearAdvection
        riemann_class = riemann.LinearAdvection
    elif problem == 'Burgers':
        solver_class = InviscidBurgers
        equation_class = equation.InviscidBurgers
        riemann_class = riemann.InviscidBurgers
    else:
        assert False
    a_const = 1.0
    solver = solver_class(a_const,
        spatial_scheme=spatial_class(
            equation_class(a_const), riemann_class(a_const),
            degree=int(argv[1]), n_element=int(argv[2]),
            x_left=float(argv[3]), x_right=float(argv[4])),
        ode_solver = temporal.RungeKutta(order=int(argv[5])))
    solver.run(t_start=float(argv[6]), t_stop=float(argv[7]),
        n_step=int(argv[8]))
