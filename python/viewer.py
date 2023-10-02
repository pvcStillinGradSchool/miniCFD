"""Plot solutions or errors in the x-t plane.
"""
import sys
import numpy as np
import vtk
from matplotlib import pyplot as plt
from matplotlib import colors


class Viewer:

    def __init__(self, expect, actual, scalar_name: str) -> None:
        self._actual_path = actual
        self._scalar_name = scalar_name
        self._actual = self._viscosity = None
        if scalar_name.startswith('Viscosity'):
            self._viscosity = Viewer.load(actual, scalar_name)
        else:
            self._expect, m_point = Viewer.load(expect, scalar_name)
            self._actual, n_point = Viewer.load(actual, scalar_name)
            assert m_point == n_point
            n_element = 101
            self._points = np.linspace(0, 2, n_point) / (2 / n_element)

    @staticmethod
    def load(path, scalar_name):
        reader = vtk.vtkXMLUnstructuredGridReader()
        n_frame = 101
        udata = None
        for i_frame in range(n_frame):
            reader.SetFileName(f"{path}/Frame{i_frame}.vtu")
            reader.Update()
            pdata = reader.GetOutput().GetPointData()
            assert isinstance(pdata, vtk.vtkPointData)
            u = pdata.GetAbstractArray(scalar_name)
            if not u:
                return None
            assert isinstance(u, vtk.vtkFloatArray)
            if udata is None:
                n_point = u.GetNumberOfTuples()
                udata = np.ndarray((n_frame, n_point))
            for i_point in range(n_point):
                udata[i_frame][i_point] = u.GetTuple1(i_point)
        return udata, n_point

    def plot_frame(self):
        if self._actual is None:
            return
        fig, ax = plt.subplots()
        ax.set_xlabel('Element Index')
        ax.set_ylabel(self._scalar_name)
        i_frame = 100
        ydata = self._expect[i_frame]
        ax.plot(self._points, ydata, 'b--', label='Expect')
        ydata = self._actual[i_frame]
        ax.plot(self._points, ydata, 'r-', label='Actual')
        plt.legend()
        plt.grid()
        plt.tight_layout()
        plt.savefig(f'{self._actual_path}/Frame{i_frame}.svg')

    def plot_error(self):
        if self._actual is None:
            return
        fig, ax = plt.subplots()
        ax.set_xlabel('Point Index')
        ax.set_ylabel('Time Index')
        zdata = np.abs(self._expect - self._actual)
        mappable = ax.imshow(zdata, origin='lower', cmap='coolwarm',
            norm=colors.LogNorm(vmin=1e-6, vmax=1e0, clip=True))
        fig.colorbar(mappable, location='top', label='Pointwise Errors')
        plt.tight_layout()
        plt.savefig(f'{self._actual_path}/error2d.svg')

    def plot_viscosity(self):
        if self._viscosity is None:
            return
        fig, ax = plt.subplots()
        ax.set_xlabel('Point Index')
        ax.set_ylabel('Time Index')
        zdata = self._viscosity
        mappable = ax.imshow(zdata, origin='lower', cmap='coolwarm')
        fig.colorbar(mappable, location='top', label=self._scalar_name)
        plt.tight_layout()
        plt.savefig(f'{self._actual_path}/{self._scalar_name}.svg')


if __name__ == '__main__':
    viewer = Viewer(sys.argv[1], sys.argv[2], sys.argv[3])
    viewer.plot_frame()
    viewer.plot_error()
    viewer.plot_viscosity()
