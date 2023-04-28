"""Put separated modules together.
"""
import abc
import argparse
import numpy as np
from matplotlib import pyplot as plt
import matplotlib.animation as mpla
from scipy import optimize

import concept
import equation
import riemann
import spatial, detector, limiter, viscous
import temporal


class SolverBase(abc.ABC):
    """Define common methods for all solvers.
    """

    def __init__(self, spatial_scheme: concept.SpatialScheme,
            detector: concept.Detector, limiter: concept.Limiter,
            viscous: concept.Viscous,
            ode_solver: concept.OdeSolver):
        self._spatial = spatial_scheme
        self._spatial.set_detector_and_limiter(detector, limiter, viscous)
        self._ode_solver = ode_solver
        self._solver_name = (f'scheme={self._spatial.name()}, ' +
            f'detector={detector.name()}, limiter={limiter.name()}, ' +
            f'viscous={viscous.name()}')
        self._animation = None

    @abc.abstractmethod
    def problem_name(self) -> str:
        """Get a string representation of the problem."""

    def solver_name(self) -> str:
        return self._solver_name

    @abc.abstractmethod
    def u_init(self, x_global):
        """Initial condition of the problem."""

    @abc.abstractmethod
    def u_exact(self, x_global, t_curr):
        """Exact solution of the problem."""

    def get_figure(self):
        fig = plt.figure(figsize=(8, 6))
        plt.xlabel(r'$x$')
        plt.ylabel(r'$u$')
        plt.ylim([-1.4, 1.6])
        plt.xticks(np.linspace(self._spatial.x_left(), self._spatial.x_right(),
            self._spatial.n_element() + 1), minor=True)
        plt.xticks(np.linspace(self._spatial.x_left(), self._spatial.x_right(),
            5), minor=False)
        plt.grid(which='minor')
        return fig

    def plot(self, t_curr, n_point: int):
        """Plot solution curves at a given moment.
        """
        points = np.linspace(self._spatial.x_left(), self._spatial.x_right(), n_point)
        expect_solution = np.ndarray(n_point)
        approx_solution = np.ndarray(n_point)
        for i in range(n_point):
            point_i = points[i]
            approx_solution[i] = self._spatial.get_solution_value(point_i)
            expect_solution[i] = self.u_exact(point_i, t_curr)
        fig = self.get_figure()
        plt.plot(points, approx_solution, 'b-+', label=self.solver_name())
        plt.plot(points, expect_solution, 'r-',
            label=f'Exact Solution of {self.problem_name()}')
        plt.title(r'$t$'+f' = {t_curr:.2f}')
        plt.legend(loc='upper right')
        plt.tight_layout()
        # plt.show()
        plt.savefig(f't={t_curr:.2f}.pdf')

    @abc.abstractmethod
    def a_max(self):
        """The maximum phase speed.
        """

    def run(self, t_start: float, t_stop: float,  n_step: int):
        """Solve the problem in a given time range and plot the results.
        """
        delta_t = (t_stop - t_start) / n_step
        self._spatial.initialize(lambda x_global: self.u_init(x_global))
        n_step = int(np.ceil((t_stop - t_start) / delta_t))
        delta_t = (t_stop - t_start) / n_step
        plot_steps = n_step // 4
        for i_step in range(n_step + 1):
            t_curr = t_start + i_step * delta_t
            print(f'step {i_step} / {n_step}, t = {t_curr:g}')
            if i_step % plot_steps == 0:
                self.plot(t_curr, n_point=201)
            if i_step < n_step:
                self._ode_solver.update(self._spatial, delta_t, t_curr)

    def get_ydata(self, t_curr, points):
        """Get the y-data for updating the solution curvess.
        """
        n_point = len(points)
        expect_solution = np.ndarray(n_point)
        approx_solution = np.ndarray(n_point)
        for i in range(n_point):
            point_i = points[i]
            approx_solution[i] = self._spatial.get_solution_value(point_i)
            expect_solution[i] = self.u_exact(point_i, t_curr)
        return expect_solution, approx_solution

    def animate(self, t_start: float, t_stop: float,  n_step: int):
        """Solve the problem in a given time range and animate the results.
        """
        delta_x = self._spatial.delta_x()
        delta_t = (t_stop - t_start) / n_step
        cfl = self.a_max() * delta_t / delta_x
        viscous = self._spatial.equation().get_diffusive_coeff()
        cell_reynolds = self.a_max() * delta_x / (viscous + 1e-18)
        cfl_max = 1 / (1 + 2 / cell_reynolds) / (1 + 2 * self._spatial.degree())
        print(f"delta_x = {delta_x}, delta_t = {delta_t}, CFL = {cfl:g},",
            f'CFL_DG(p)_RK(p+1) = {cfl_max:g}')
        fig = self.get_figure()
        # initialize line-plot objects
        approx_line, = plt.plot([], [], 'b-+', label=self.solver_name())
        expect_line, = plt.plot([], [], 'r-',
            label=f'Exact Solution of {self.problem_name()}')
        points = np.linspace(self._spatial.x_left(), self._spatial.x_right(), 201)
        # initialize animation
        def init_func():
            self._spatial.initialize(lambda x_global: self.u_init(x_global))
            expect_line.set_xdata(points)
            approx_line.set_xdata(points)
        # update data for the next frame
        def func(t_curr):
            expect_solution, approx_solution = self.get_ydata(t_curr, points)
            expect_line.set_ydata(expect_solution)
            approx_line.set_ydata(approx_solution)
            plt.title(r'$t$'+f' = {t_curr:.2f}')
            plt.legend(loc='upper right')
            self._ode_solver.update(self._spatial, delta_t, t_curr)
        frames = np.linspace(t_start, t_stop, n_step)
        self._animation = mpla.FuncAnimation(
            plt.gcf(), func, frames, init_func, interval=5)
        plt.show()


class LinearSmooth(SolverBase):
    """Demo the usage of LinearAdvection related classes for smooth IC.
    """

    def __init__(self, wave_number: int,
            spatial_scheme: concept.SpatialScheme,
            detector: concept.Detector, limiter: concept.Limiter,
            viscous: concept.Viscous,
            ode_solver: concept.OdeSolver) -> None:
        super().__init__(spatial_scheme, detector, limiter, viscous, ode_solver)
        self._a_const = self._spatial.equation().get_convective_jacobian()
        self._b_const = self._spatial.equation().get_diffusive_coeff()
        self._k_const = wave_number
        self._wave_number = self._k_const * np.pi * 2 / self._spatial.length()

    def problem_name(self):
        my_name = self._spatial.equation().name() + r', $u(x,t=0)=\sin($'
        length = self._spatial.length() / 2
        my_name += f'{self._k_const:g}' + r'$\pi x/$' + f'{length:g}' + r'$)$'
        return my_name

    def a_max(self):
        return np.abs(self._a_const)

    def u_init(self, x_global):
        x_global = x_global - self._spatial.x_left()
        value = np.sin(x_global * self._wave_number)
        return value

    def u_exact(self, x_global, t_curr):
        ratio = np.exp(-t_curr * self._b_const * self._wave_number**2)
        return ratio * self.u_init(x_global - self._a_const * t_curr)


class LinearJumps(LinearSmooth):
    """Demo the usage of LinearAdvection related classes for IC with jumps.
    """

    def __init__(self, wave_number: int,
            spatial_scheme: concept.SpatialScheme,
            detector: concept.Detector, limiter: concept.Limiter,
            viscous: concept.Viscous,
            ode_solver: concept.OdeSolver) -> None:
        LinearSmooth.__init__(self, wave_number, spatial_scheme,
            detector, limiter, viscous, ode_solver)

    def u_init(self, x_global):
        value = LinearSmooth.u_init(self, x_global)
        return np.sign(value)

    def problem_name(self):
        my_name = self._spatial.equation().name() + r', $u(x,t=0)=$sign$(\sin($'
        length = self._spatial.length() / 2
        my_name += f'{self._k_const:g}' + r'$\pi x/$' + f'{length:g}' + r'$))$'
        return my_name


class InviscidBurgers(SolverBase):
    """Demo the usage of InviscidBurgers related classes.
    """

    def __init__(self, wave_number: int,
            spatial_scheme: concept.SpatialScheme,
            detector: concept.Detector, limiter: concept.Limiter,
            viscous: concept.Viscous,
            ode_solver: concept.OdeSolver) -> None:
        super().__init__(spatial_scheme, detector, limiter, viscous, ode_solver)
        self._wave_number = 1.0 * np.pi * 2 / self._spatial.length()
        self._u_prev = 0.0
        self._x_mid = (self._spatial.x_left() + self._spatial.x_right()) / 2

    def problem_name(self) -> str:
        my_name = self._spatial.equation().name() + r', $u(x,t=0)=\sin($'
        length = self._spatial.length() / 2
        my_name += r'$\pi x/$' + f'{length:g}' + r'$)$'
        return my_name

    def a_max(self):
        return self._spatial.equation().get_convective_jacobian(u=1.0)

    def u_init(self, x_global):
        x_global = x_global - self._spatial.x_left()
        value = np.sin(x_global * self._wave_number)
        return value

    def u_exact(self, x_global, t_curr):
        # Solve u from u_curr = u_init(x - a(u_curr) * t_curr).
        def func(u_curr):
            ku = self._spatial.equation().get_convective_jacobian(u_curr)
            return u_curr - self.u_init(x_global - ku * t_curr)
        if np.abs(x_global - self._x_mid) < 1e-6:
            root = 0.0
        else:
            if x_global > self._x_mid and self._u_prev > 0:
                self._u_prev = -self._u_prev
            delta = 0.1
            u_min = self._u_prev - delta
            u_max = self._u_prev + delta
            while func(u_min) * func(u_max) > 0:
                u_min -= delta
                u_max += delta
            root = optimize.bisect(func, u_min, u_max)
            self._u_prev = root
        return root


class EulerRiemann(SolverBase):
    """Demo the usage of Euler related classes.
    """

    def __init__(self,
            spatial_scheme: concept.SpatialScheme,
            detector: concept.Detector, limiter: concept.Limiter,
            viscous: concept.Viscous,
            ode_solver: concept.OdeSolver) -> None:
        super().__init__(spatial_scheme, detector, limiter, viscous, ode_solver)
        self._x_mid = (self._spatial.x_left() + self._spatial.x_right()) / 2
        self._equation = equation.Euler(gamma=1.4)
        self._riemann = riemann.Euler(gamma=1.4)
        self._value_left = self._equation.primitive_to_conservative(
              rho=1.0, u=0, p=1.0)
        self._value_right = self._equation.primitive_to_conservative(
              rho=0.125, u=0, p=0.1)
        self._riemann.set_initial(self._value_left, self._value_right)

    def a_max(self):
        return 1.0

    def u_init(self, x_global):
        if x_global < self._x_mid:
            return self._value_left
        else:
            return self._value_right

    def u_exact(self, x_global, t_curr):
        return self._riemann.U(x_global - self._x_mid, t_curr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog = 'python3 solver.py',
        description = 'What the program does',
        epilog = 'Text at the bottom of help')
    parser.add_argument('-n', '--n_element',
        default=23, type=int,
        help='number of elements')
    parser.add_argument('-l', '--x_left',
        default=-1.0, type=float,
        help='coordinate of the left end of the domain')
    parser.add_argument('-r', '--x_right',
        default=+1.0, type=float,
        help='coordinate of the right end of the domain')
    parser.add_argument('-o', '--rk_order',
        default=3, type=int,
        help='order of Runge--Kutta scheme')
    parser.add_argument('-b', '--t_begin',
        default=0.0, type=float,
        help='time to start')
    parser.add_argument('-e', '--t_end',
        default=10.0, type=float,
        help='time to stop')
    parser.add_argument('-s', '--n_step',
        default=500, type=int,
        help='number of time steps')
    parser.add_argument('-m', '--method',
        choices=['LagrangeDG', 'LagrangeFR', 'LegendreDG', 'LegendreFR',
            'DGwithFR'],
        default='LegendreFR',
        help='method for spatial discretization')
    parser.add_argument('--detector',
        choices=['Off', 'All', 'KXCRF2004', 'Li2011', 'Zhu2021'],
        default='Off',
        help='method for detecting jumps')
    parser.add_argument('--limiter',
        choices=['Li2020', 'Zhong2013', 'Xu2023', 'Off'],
        default='Off',
        help='method for limiting numerical oscillations')
    parser.add_argument('--viscous_model',
        choices=['Off', 'Persson2006'],
        default='Off',
        help='method for adding artificial viscosity')
    parser.add_argument('-d', '--degree',
        default=2, type=int,
        help='degree of polynomials for approximation')
    parser.add_argument('-p', '--problem',
        choices=['Smooth', 'Jumps', 'Burgers', 'Euler'],
        default='Smooth',
        help='problem to be solved')
    parser.add_argument('-a', '--phase_speed',
        default=1.0, type=float,
        help='phase speed of the wave')
    parser.add_argument('-v', '--viscous_coeff',
        default=0.0, type=float,
        help='viscous coeff of the equation')
    parser.add_argument('-k', '--wave_number',
        default=1, type=int,
        help='number of waves in the domain')
    parser.add_argument('--animate', action='store_true')
    args = parser.parse_args()
    print(args)
    if args.method == 'LagrangeDG':
        SpatialClass = spatial.LagrangeDG
    elif args.method == 'LagrangeFR':
        SpatialClass = spatial.LagrangeFR
    elif args.method == 'LegendreDG':
        SpatialClass = spatial.LegendreDG
    elif args.method == 'LegendreFR':
        SpatialClass = spatial.LegendreFR
    elif args.method == 'DGwithFR':
        SpatialClass = spatial.DGwithFR
    else:
        assert False
    if args.detector == 'Off':
        DetectorClass = detector.Off
    elif args.detector == 'All':
        DetectorClass = detector.All
    elif args.detector == 'KXCRF2004':
        DetectorClass = detector.Krivodonova2004
    elif args.detector == 'Li2011':
        DetectorClass = detector.LiRen2011
    elif args.detector == 'Zhu2021':
        DetectorClass = detector.ZhuShuQiu2021
    else:
        assert False
    if args.limiter == 'Li2020':
        LimiterClass = limiter.LiWangRen2020
    elif args.limiter == 'Zhong2013':
        LimiterClass = limiter.ZhongShu2013
    elif args.limiter == 'Xu2023':
        LimiterClass = limiter.Xu2023
    elif args.limiter == 'Off':
        LimiterClass = limiter.Off
    else:
        assert False
    if args.viscous_model == 'Off':
        ViscousClass = viscous.Off
    elif args.viscous_model == 'Persson2006':
        ViscousClass = viscous.Persson2006
    else:
        assert False
    if args.problem == 'Smooth':
        SolverClass = LinearSmooth
        the_equation = equation.LinearAdvectionDiffusion(args.phase_speed,
            args.viscous_coeff)
        the_riemann = riemann.LinearAdvection(args.phase_speed)
    elif args.problem == 'Jumps':
        SolverClass = LinearJumps
        the_equation = equation.LinearAdvection(args.phase_speed)
        the_riemann = riemann.LinearAdvection(args.phase_speed)
    elif args.problem == 'Burgers':
        SolverClass = InviscidBurgers
        the_equation = equation.InviscidBurgers(args.phase_speed)
        the_riemann = riemann.InviscidBurgers(args.phase_speed)
    elif args.problem == 'Euler':
        SolverClass = EulerRiemann
        the_equation = equation.Euler(gamma=1.4)
        the_riemann = riemann.Euler(gamma=1.4)
    else:
        assert False
    solver = SolverClass(args.wave_number,
        spatial_scheme=SpatialClass(the_equation, the_riemann,
            args.degree, args.n_element, args.x_left, args.x_right),
        detector=DetectorClass(), limiter=LimiterClass(),
        viscous=ViscousClass(),
        ode_solver=temporal.RungeKutta(args.rk_order))
    if args.animate:
        solver.animate(args.t_begin, args.t_end, args.n_step)
    else:
        solver.run(args.t_begin, args.t_end, args.n_step)

