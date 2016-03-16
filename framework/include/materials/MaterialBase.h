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

#ifndef MATERIALBASE_H
#define MATERIALBASE_H

// MOOOSE includes
#include "MooseObject.h"
#include "BlockRestrictable.h"
#include "BoundaryRestrictable.h"
#include "SetupInterface.h"
#include "Coupleable.h"
#include "MooseVariableDependencyInterface.h"
#include "ScalarCoupleable.h"
#include "FunctionInterface.h"
#include "UserObjectInterface.h"
#include "TransientInterface.h"
#include "MaterialPropertyInterface.h"
#include "PostprocessorInterface.h"
#include "DependencyResolverInterface.h"
#include "Restartable.h"
#include "ZeroInterface.h"
#include "MeshChangedInterface.h"
#include "OutputInterface.h"
#include "RandomInterface.h"
#include "MaterialProperty.h"

// forward declarations
class MaterialBase;
class MooseMesh;
class MaterialData;
class SubProblem;

template<>
InputParameters validParams<MaterialBase>();

/**
 * Base for Materials and DiscreateMaterial objects.
 *   Material - computed by MOOSE
 *   DiscreteMaterial - computed by explicit calls (see MaterialPropertyInterface::getDiscreteMaterial)
 */
class MaterialBase :
    public MooseObject,
    public BlockRestrictable,
    public BoundaryRestrictable,
    public SetupInterface,
    public Coupleable,
    public MooseVariableDependencyInterface,
    public ScalarCoupleable,
    public FunctionInterface,
    public UserObjectInterface,
    public TransientInterface,
    public MaterialPropertyInterface,
    public PostprocessorInterface,
    public DependencyResolverInterface,
    public Restartable,
    public ZeroInterface,
    public MeshChangedInterface,
    public OutputInterface,
    public RandomInterface
{
public:

  /**
   * Constructor
   */
  MaterialBase(const InputParameters & parameters);

  /**
   * Initialize stateful properties (if material has some)
   */
  virtual void initStatefulProperties(unsigned int n_points);

  ///@{
  /**
   * Retrieve the property throgh a given input parameter key with a fallback
   * to getting it by name
   */
  template<typename T>
  const MaterialProperty<T> & getMaterialProperty(const std::string & name);
  template<typename T>
  const MaterialProperty<T> & getMaterialPropertyOld(const std::string & name);
  template<typename T>
  const MaterialProperty<T> & getMaterialPropertyOlder(const std::string & name);
  ///@}

  ///@{
  /**
   * Retrieve the property named "name"
   */
  template<typename T>
  const MaterialProperty<T> & getMaterialPropertyByName(const std::string & prop_name);
  template<typename T>
  const MaterialProperty<T> & getMaterialPropertyOldByName(const std::string & prop_name);
  template<typename T>
  const MaterialProperty<T> & getMaterialPropertyOlderByName(const std::string & prop_name);
  ///@}

  ///@{
  /**
   * Declare the property named "name"
   */
  template<typename T>
  MaterialProperty<T> & declareProperty(const std::string & prop_name);
  template<typename T>
  MaterialProperty<T> & declarePropertyOld(const std::string & prop_name);
  template<typename T>
  MaterialProperty<T> & declarePropertyOlder(const std::string & prop_name);
  ///@}

  /**
   * Return a material property that is initialized to zero by default and does
   * not need to (but can) be declared by another material.
   */
  template<typename T>
  const MaterialProperty<T> & getZeroMaterialProperty(const std::string & prop_name);

  /**
   * Return a set of properties accessed with getMaterialProperty
   * @return A reference to the set of properties with calls to getMaterialProperty
   */
  virtual
  const std::set<std::string> &
  getRequestedItems() { return _requested_props; }

  /**
   * Return a set of properties accessed with declareProperty
   * @return A reference to the set of properties with calls to declareProperty
   */
  virtual
  const std::set<std::string> &
  getSuppliedItems() { return _supplied_props; }

  void checkStatefulSanity() const;

  /**
   * Check if a material property is valid for all blocks of this Material
   *
   * This method returns true if the supplied property name has been declared
   * in a Material object on the block ids for this object.
   *
   * @param prop_name the name of the property to query
   * @return true if the property exists for all block ids of the object, otherwise false
   *
   * @see BlockRestrictable::hasBlockMaterialPropertyHelper
   */
  virtual bool hasBlockMaterialPropertyHelper(const std::string & prop_name);

  /**
   * Get the list of output objects that this class is restricted
   * @return A vector of OutputNames
   */
  std::set<OutputName> getOutputs();

  /**
   * Returns true of the MaterialData type is not associated with volume data
   */
  bool isBoundaryMaterial() { return _bnd; }

protected:
  SubProblem & _subproblem;

  FEProblem & _fe_problem;
  THREAD_ID _tid;
  Assembly & _assembly;

  bool _bnd;
  bool _neighbor;

  unsigned int _qp;

  QBase * & _qrule;
  const MooseArray<Real> & _JxW;
  const MooseArray<Real> & _coord;
  const MooseArray<Point> & _q_point;
  /// normals at quadrature points (valid only in boundary materials)
  const MooseArray<Point> & _normals;

  const Elem * & _current_elem;

  /// current side of the current element
  unsigned int & _current_side;

  MooseMesh & _mesh;

  /// Coordinate system
  const Moose::CoordinateSystemType & _coord_sys;

  /// Set of properties accessed via get method
  std::set<std::string> _requested_props;

  /// Set of properties declared
  std::set<std::string> _supplied_props;

  enum QP_Data_Type {
    CURR,
    PREV
  };

  enum Prop_State
  {
    CURRENT = 0x1,
    OLD = 0x2,
    OLDER = 0x4
  };
  std::map<std::string, int> _props_to_flags;

  /**
   * Initialize stateful properties at quadrature points.  Note when using this function you only need to address
   * the "current" material properties not the old ones directly, i.e. if you have a property named "_diffusivity"
   * and an older property named "_diffusivity_old".  You only need to initialize diffusivity.  MOOSE will use
   * copy that initial value to the old and older values as necessary.
   */
  virtual void initQpStatefulProperties();

  /**
   * Compute material properties in quadrature point
   * Materials has to redefine this.
   */
  virtual void computeQpProperties() = 0;

private:
  /**
   * Small helper function to call storeMatPropName
   */
  void registerPropName(std::string prop_name, bool is_get, Prop_State state);

  bool _has_stateful_property;
};

template<typename T>
const MaterialProperty<T> &
MaterialBase::getMaterialProperty(const std::string & name)
{
  // Check if the supplied parameter is a valid imput parameter key
  std::string prop_name = deducePropertyName(name);

  // Check if it's just a constant.
  const MaterialProperty<T> * default_property = defaultMaterialProperty<T>(prop_name);
  if (default_property)
    return *default_property;

  return getMaterialPropertyByName<T>(prop_name);
}

template<typename T>
const MaterialProperty<T> &
MaterialBase::getMaterialPropertyOld(const std::string & name)
{
  // Check if the supplied parameter is a valid imput parameter key
  std::string prop_name = deducePropertyName(name);

  // Check if it's just a constant.
  const MaterialProperty<T> * default_property = defaultMaterialProperty<T>(prop_name);
  if (default_property)
    return *default_property;

  return getMaterialPropertyOldByName<T>(prop_name);
}

template<typename T>
const MaterialProperty<T> &
MaterialBase::getMaterialPropertyOlder(const std::string & name)
{
  // Check if the supplied parameter is a valid imput parameter key
  std::string prop_name = deducePropertyName(name);

  // Check if it's just a constant.
  const MaterialProperty<T> * default_property = defaultMaterialProperty<T>(prop_name);
  if (default_property)
    return *default_property;

  return getMaterialPropertyOlderByName<T>(prop_name);
}

template<typename T>
const MaterialProperty<T> &
MaterialBase::getMaterialPropertyByName(const std::string & prop_name)
{
  // The property may not exist yet, so declare it (declare/getMaterialProperty are referencing the same memory)
  _requested_props.insert(prop_name);
  registerPropName(prop_name, true, MaterialBase::CURRENT);
  _fe_problem.markMatPropRequested(prop_name);
  return _material_data->getProperty<T>(prop_name);
}

template<typename T>
const MaterialProperty<T> &
MaterialBase::getMaterialPropertyOldByName(const std::string & prop_name)
{
  _requested_props.insert(prop_name);
  registerPropName(prop_name, true, MaterialBase::OLD);
  _fe_problem.markMatPropRequested(prop_name);
  return _material_data->getPropertyOld<T>(prop_name);
}

template<typename T>
const MaterialProperty<T> &
MaterialBase::getMaterialPropertyOlderByName(const std::string & prop_name)
{
  _requested_props.insert(prop_name);
  registerPropName(prop_name, true, MaterialBase::OLDER);
  _fe_problem.markMatPropRequested(prop_name);
  return _material_data->getPropertyOlder<T>(prop_name);
}


template<typename T>
MaterialProperty<T> &
MaterialBase::declareProperty(const std::string & prop_name)
{
  registerPropName(prop_name, false, MaterialBase::CURRENT);
  return _material_data->declareProperty<T>(prop_name);
}

template<typename T>
MaterialProperty<T> &
MaterialBase::declarePropertyOld(const std::string & prop_name)
{
  registerPropName(prop_name, false, MaterialBase::OLD);
  return _material_data->declarePropertyOld<T>(prop_name);
}

template<typename T>
MaterialProperty<T> &
MaterialBase::declarePropertyOlder(const std::string & prop_name)
{
  registerPropName(prop_name, false, MaterialBase::OLDER);
  return _material_data->declarePropertyOlder<T>(prop_name);
}

template<typename T>
const MaterialProperty<T> &
MaterialBase::getZeroMaterialProperty(const std::string & prop_name)
{
  MaterialProperty<T> & preload_with_zero = _material_data->getProperty<T>(prop_name);

  _requested_props.insert(prop_name);
  registerPropName(prop_name, true, MaterialBase::CURRENT);
  _fe_problem.markMatPropRequested(prop_name);

  // Register this material on these blocks and boundaries as a zero property with relaxed consistency checking
  for (std::set<SubdomainID>::const_iterator it = blockIDs().begin(); it != blockIDs().end(); ++it)
    _fe_problem.storeZeroMatProp(*it, prop_name);
  for (std::set<BoundaryID>::const_iterator it = boundaryIDs().begin(); it != boundaryIDs().end(); ++it)
    _fe_problem.storeZeroMatProp(*it, prop_name);

  // set values for all qpoints to zero
  unsigned int nqp = _mi_feproblem.getMaxQps();
  preload_with_zero.resize(nqp);
  for (unsigned int qp = 0; qp < nqp; ++qp)
    mooseSetToZero<T>(preload_with_zero[qp]);

  return preload_with_zero;
}

#endif // MATERIALBASE_H