/****************************************************************/
/*               DO NOT MODIFY THIS HEADER                      */
/* MOOSE - Multiphysics Object Oriented Simulation Environment  */
/*                                                              */
/*           (c) 2010 Battelle Energy Alliance, LLC             */
/*                   ALL RIGHTS RESERVED                        */
/*                                                              */
/*          Prepared by Battelle Energy Alliance, LLC           */
/*            Under Contract No. DE-AC07-05ID14517              */
/*            With the U. S. Department of Energy               */
/*                                                              */
/*            See COPYRIGHT for full restrictions               */
/****************************************************************/

#include "MultiAppUserObjectTransfer.h"

// MOOSE includes
#include "DisplacedProblem.h"
#include "FEProblem.h"
#include "MooseMesh.h"
#include "MooseTypes.h"
#include "MooseVariable.h"
#include "MultiApp.h"
#include "UserObject.h"

// libMesh
#include "libmesh/meshfree_interpolation.h"
#include "libmesh/system.h"
#include "libmesh/mesh_function.h"
#include "libmesh/mesh_tools.h"

template <>
InputParameters
validParams<MultiAppUserObjectTransfer>()
{
  InputParameters params = validParams<MultiAppTransfer>();
  params.addRequiredParam<AuxVariableName>(
      "variable", "The auxiliary variable to store the transferred values in.");
  params.addRequiredParam<UserObjectName>(
      "user_object",
      "The UserObject you want to transfer values from.  Note: This might be a "
      "UserObject from your MultiApp's input file!");

  params.addParam<bool>("displaced_target_mesh",
                        false,
                        "Whether or not to use the displaced mesh for the target mesh.");

  return params;
}

MultiAppUserObjectTransfer::MultiAppUserObjectTransfer(const InputParameters & parameters)
  : MultiAppTransfer(parameters),
    _to_var_name(getParam<AuxVariableName>("variable")),
    _user_object_name(getParam<UserObjectName>("user_object")),
    _displaced_target_mesh(getParam<bool>("displaced_target_mesh"))
{
  // This transfer does not work with DistributedMesh
  _fe_problem.mesh().errorIfDistributedMesh("MultiAppUserObjectTransfer");
}

void
MultiAppUserObjectTransfer::initialSetup()
{
  if (_direction == TO_MULTIAPP)
    variableIntegrityCheck(_to_var_name);
}

void
MultiAppUserObjectTransfer::execute()
{
  _console << "Beginning MultiAppUserObjectTransfer " << name() << std::endl;

  switch (_direction)
  {
    case TO_MULTIAPP:
    {
      for (unsigned int i = 0; i < _multi_app->numGlobalApps(); i++)
      {
        if (_multi_app->hasLocalApp(i))
        {
          Moose::ScopedCommSwapper swapper(_multi_app->comm());

          // Loop over the master nodes and set the value of the variable
          System * to_sys = find_sys(_multi_app->appProblemBase(i).es(), _to_var_name);

          unsigned int sys_num = to_sys->number();
          unsigned int var_num = to_sys->variable_number(_to_var_name);

          NumericVector<Real> & solution = _multi_app->appTransferVector(i, _to_var_name);

          MeshBase * mesh = NULL;

          if (_displaced_target_mesh && _multi_app->appProblemBase(i).getDisplacedProblem())
          {
            mesh = &_multi_app->appProblemBase(i).getDisplacedProblem()->mesh().getMesh();
          }
          else
            mesh = &_multi_app->appProblemBase(i).mesh().getMesh();

          bool is_nodal = to_sys->variable_type(var_num).family == LAGRANGE;

          const UserObject & user_object =
              _multi_app->problemBase().getUserObjectBase(_user_object_name);

          if (is_nodal)
          {
            MeshBase::const_node_iterator node_it = mesh->local_nodes_begin();
            MeshBase::const_node_iterator node_end = mesh->local_nodes_end();

            for (; node_it != node_end; ++node_it)
            {
              Node * node = *node_it;

              if (node->n_dofs(sys_num, var_num) > 0) // If this variable has dofs at this node
              {
                // The zero only works for LAGRANGE!
                dof_id_type dof = node->dof_number(sys_num, var_num, 0);

                swapper.forceSwap();
                Real from_value = user_object.spatialValue(*node + _multi_app->position(i));
                swapper.forceSwap();

                solution.set(dof, from_value);
              }
            }
          }
          else // Elemental
          {
            MeshBase::const_element_iterator elem_it = mesh->local_elements_begin();
            MeshBase::const_element_iterator elem_end = mesh->local_elements_end();

            for (; elem_it != elem_end; ++elem_it)
            {
              Elem * elem = *elem_it;

              Point centroid = elem->centroid();

              if (elem->n_dofs(sys_num, var_num) > 0) // If this variable has dofs at this elem
              {
                // The zero only works for LAGRANGE!
                dof_id_type dof = elem->dof_number(sys_num, var_num, 0);

                swapper.forceSwap();
                Real from_value = user_object.spatialValue(centroid + _multi_app->position(i));
                swapper.forceSwap();

                solution.set(dof, from_value);
              }
            }
          }

          solution.close();
          to_sys->update();
        }
      }

      break;
    }
    case FROM_MULTIAPP:
    {
      FEProblemBase & to_problem = _multi_app->problemBase();
      MooseVariable & to_var = to_problem.getVariable(0, _to_var_name);
      SystemBase & to_system_base = to_var.sys();

      System & to_sys = to_system_base.system();

      unsigned int to_sys_num = to_sys.number();

      // Only works with a serialized mesh to transfer to!
      mooseAssert(to_sys.get_mesh().is_serial(),
                  "MultiAppUserObjectTransfer only works with ReplicatedMesh!");

      unsigned int to_var_num = to_sys.variable_number(to_var.name());

      _console << "Transferring to: " << to_var.name() << std::endl;

      // EquationSystems & to_es = to_sys.get_equation_systems();

      // Create a serialized version of the solution vector
      NumericVector<Number> * to_solution = to_sys.solution.get();

      MeshBase * to_mesh = NULL;

      if (_displaced_target_mesh && to_problem.getDisplacedProblem())
        to_mesh = &to_problem.getDisplacedProblem()->mesh().getMesh();
      else
        to_mesh = &to_problem.mesh().getMesh();

      bool is_nodal = to_sys.variable_type(to_var_num).family == LAGRANGE;

      for (unsigned int i = 0; i < _multi_app->numGlobalApps(); i++)
      {
        if (!_multi_app->hasLocalApp(i))
          continue;

        Point app_position = _multi_app->position(i);
        BoundingBox app_box = _multi_app->getBoundingBox(i);
        const UserObject & user_object = _multi_app->appUserObjectBase(i, _user_object_name);

        if (is_nodal)
        {
          MeshBase::const_node_iterator node_it = to_mesh->nodes_begin();
          MeshBase::const_node_iterator node_end = to_mesh->nodes_end();

          for (; node_it != node_end; ++node_it)
          {
            Node * node = *node_it;

            if (node->n_dofs(to_sys_num, to_var_num) > 0) // If this variable has dofs at this node
            {
              // See if this node falls in this bounding box
              if (app_box.contains_point(*node))
              {
                dof_id_type dof = node->dof_number(to_sys_num, to_var_num, 0);

                Real from_value = 0;
                {
                  Moose::ScopedCommSwapper swapper(_multi_app->comm());
                  from_value = user_object.spatialValue(*node - app_position);
                }

                to_solution->set(dof, from_value);
              }
            }
          }
        }
        else // Elemental
        {
          MeshBase::const_element_iterator elem_it = to_mesh->elements_begin();
          MeshBase::const_element_iterator elem_end = to_mesh->elements_end();

          for (; elem_it != elem_end; ++elem_it)
          {
            Elem * elem = *elem_it;

            if (elem->n_dofs(to_sys_num, to_var_num) > 0) // If this variable has dofs at this elem
            {
              Point centroid = elem->centroid();

              // See if this elem falls in this bounding box
              if (app_box.contains_point(centroid))
              {
                dof_id_type dof = elem->dof_number(to_sys_num, to_var_num, 0);

                Real from_value = 0;
                {
                  Moose::ScopedCommSwapper swapper(_multi_app->comm());
                  from_value = user_object.spatialValue(centroid - app_position);
                }

                to_solution->set(dof, from_value);
              }
            }
          }
        }
      }

      to_solution->close();
      to_sys.update();

      break;
    }
  }

  _console << "Finished MultiAppUserObjectTransfer " << name() << std::endl;
}
