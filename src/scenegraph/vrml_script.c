/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) Jean Le Feuvre 2000-2005
 *					All rights reserved
 *
 *  This file is part of GPAC / Scene Graph sub-project
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *   
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <gpac/internal/scenegraph_dev.h>
/*MPEG4 & X3D tags (for node tables & script handling)*/
#include <gpac/nodes_mpeg4.h>
#include <gpac/nodes_x3d.h>


static u32 script_get_nb_static_field(GF_Node *node) 
{
	return (node->sgprivate->tag==TAG_MPEG4_Script) ? 3 : 4;
}

Bool gf_sg_has_scripting()
{
#ifdef GPAC_HAS_SPIDERMONKEY
	return 1;
#else
	return 0;
#endif
}

void Script_PreDestroy(GF_Node *node)
{
	GF_ScriptPriv *priv;
	GF_ScriptField *field;
	priv = node->sgprivate->privateStack;
	
	if (priv->JS_PreDestroy) priv->JS_PreDestroy(node);

	//destroy extra fields
	while (gf_list_count(priv->fields)) {
		field = gf_list_get(priv->fields, 0);
		gf_list_rem(priv->fields, 0);
		if (field->pField) {
			//if Node unregister
			switch (field->fieldType) {
			//specific case for GF_Node in script
			case GF_SG_VRML_SFNODE:
				gf_node_unregister((GF_Node *) field->pField, node);
				break;
			case GF_SG_VRML_MFNODE:
				gf_node_unregister_children(node, (GF_List*) field->pField);
				gf_list_del((GF_List*)field->pField);
				break;
			default:
				gf_sg_vrml_field_pointer_del(field->pField, field->fieldType);
				break;
			}
		}
		if (field->name) free(field->name);
		free(field);
	}
	gf_list_del(priv->fields);
	free(priv);
}

u32 gf_sg_script_get_num_fields(GF_Node *node, u8 IndexMode)
{
	u32 nb_static;
	GF_ScriptPriv *priv = node->sgprivate->privateStack;
	switch (IndexMode) {
	case GF_SG_FIELD_CODING_IN:
		return priv->numIn;
	case GF_SG_FIELD_CODING_OUT:
		return priv->numOut;
	case GF_SG_FIELD_CODING_DEF:
		return priv->numDef;
	case GF_SG_FIELD_CODING_DYN:
		return 0;
	default:
		nb_static = script_get_nb_static_field(node);
		return priv ? gf_list_count(priv->fields) + nb_static : nb_static;
	}
}

GF_Err gf_sg_script_get_field_index(GF_Node *node, u32 inField, u8 IndexMode, u32 *allField)
{
	u32 i;
	GF_ScriptField *sf;
	u32 nb_static = script_get_nb_static_field(node);
	GF_ScriptPriv *priv = node->sgprivate->privateStack;
	i=0;
	while ((sf = gf_list_enum(priv->fields, &i))) {
		*allField = i-1+nb_static;
		switch (IndexMode) {
		case GF_SG_FIELD_CODING_IN:
			if ((u32)sf->IN_index==inField) return GF_OK;
			break;
		case GF_SG_FIELD_CODING_DEF:
			if ((u32)sf->DEF_index==inField) return GF_OK;
			break;
		case GF_SG_FIELD_CODING_OUT:
			if ((u32)sf->OUT_index==inField) return GF_OK;
			break;
		case GF_SG_FIELD_CODING_DYN:
			return GF_BAD_PARAM;
		default:
			if (inField==i-1+nb_static) return GF_OK;
			break;
		}
	}
	/*try with default*/
#ifdef GF_NODE_USE_POINTERS
	return priv->gf_sg_script_get_field_index(node, inField, IndexMode, allField);
#else
	return gf_sg_mpeg4_node_get_field_index(node, inField, IndexMode, allField);
#endif
}


GF_Err gf_sg_script_get_field(GF_Node *node, GF_FieldInfo *info)
{
	GF_ScriptField *field;
	GF_ScriptPriv *priv;
	u32 nb_static;

	if (!info || !node) return GF_BAD_PARAM;

	priv = gf_node_get_private(node);
	nb_static = script_get_nb_static_field(node);

	//static fields
	if (info->fieldIndex < nb_static) {
#ifdef GF_NODE_USE_POINTERS
		return priv->gf_sg_script_get_field(node, info);
#else
		if (nb_static==3) return gf_sg_mpeg4_node_get_field(node, info);
		return gf_sg_x3d_node_get_field(node, info);
#endif
	}

	//dyn fields
	field = gf_list_get(priv->fields, info->fieldIndex - nb_static);
	if (!field) return GF_BAD_PARAM;

	info->eventType = field->eventType;
	info->fieldType = field->fieldType;
	info->name = field->name;
	//we need the eventIn name to activate the function...
	info->on_event_in = NULL;

	//setup pointer (special cases for nodes)
	switch (field->fieldType) {
	case GF_SG_VRML_SFNODE:
	case GF_SG_VRML_MFNODE:
		info->far_ptr = &field->pField;
		info->NDTtype = NDT_SFWorldNode;
		break;
	default:
		info->far_ptr = field->pField;
		break;
	}
	return GF_OK;
}



void gf_sg_script_init(GF_Node *node)
{
	GF_ScriptPriv *priv;


	GF_SAFEALLOC(priv, sizeof(GF_ScriptPriv));
	priv->fields = gf_list_new();

	gf_node_set_private(node, priv);
	node->sgprivate->PreDestroyNode = Script_PreDestroy;

#ifdef GF_NODE_USE_POINTERS
	/*store original table and provide replacement */
	priv->gf_sg_script_get_field = node->sgprivate->get_field;
	node->sgprivate->get_field = gf_sg_script_get_field;
	node->sgprivate->get_field_count = gf_sg_script_get_num_fields;
#endif

	//URL is exposedField (in, out Def)
	priv->numDef = priv->numIn = priv->numOut = script_get_nb_static_field(node) - 2;
	//directOutput and mustEvaluate are fields (def)
	priv->numDef += 2;
}


GF_ScriptField *gf_sg_script_field_new(GF_Node *node, u32 eventType, u32 fieldType, const char *name)
{
	GF_ScriptPriv *priv;
	GF_ScriptField *field;
	if (!name || ((node->sgprivate->tag != TAG_MPEG4_Script) && (node->sgprivate->tag != TAG_X3D_Script)))
		return NULL;

	if (eventType > GF_SG_SCRIPT_TYPE_EVENT_OUT) return NULL;
	priv = gf_node_get_private(node);

	GF_SAFEALLOC(field, sizeof(GF_ScriptField));
	field->fieldType = fieldType;
	field->name = strdup(name);

	field->DEF_index = field->IN_index = field->OUT_index = -1;
	switch (eventType) {
	case GF_SG_SCRIPT_TYPE_FIELD:
		field->DEF_index = priv->numDef;
		priv->numDef++;
		field->eventType = GF_SG_EVENT_FIELD;
		break;
	case GF_SG_SCRIPT_TYPE_EVENT_IN:
		field->IN_index = priv->numIn;
		priv->numIn++;
		field->eventType = GF_SG_EVENT_IN;
		break;
	case GF_SG_SCRIPT_TYPE_EVENT_OUT:
		field->OUT_index = priv->numOut;
		field->eventType = GF_SG_EVENT_OUT;
		priv->numOut++;
		break;
	}
	//+ static fields
	field->ALL_index = script_get_nb_static_field(node) + gf_list_count(priv->fields);
	gf_list_add(priv->fields, field);

	//create field entry
	if (fieldType != GF_SG_VRML_SFNODE) {
		field->pField = gf_sg_vrml_field_pointer_new(fieldType);
	}
	
	return field;
}


GF_Err gf_sg_script_prepare_clone(GF_Node *dest, GF_Node *orig)
{
	u32 i, type;
	GF_ScriptField *sf;
	GF_ScriptPriv *dest_priv, *orig_priv;
	orig_priv = orig->sgprivate->privateStack;
	dest_priv = dest->sgprivate->privateStack;
	if (!orig_priv || !dest_priv) return GF_BAD_PARAM;

	i=0;
	while ((sf = gf_list_enum(orig_priv->fields, &i))) {
		switch (sf->eventType) {
		case GF_SG_EVENT_IN:
			type = GF_SG_SCRIPT_TYPE_EVENT_IN;
			break;
		case GF_SG_EVENT_OUT:
			type = GF_SG_SCRIPT_TYPE_EVENT_OUT;
			break;
		case GF_SG_EVENT_FIELD:
			type = GF_SG_SCRIPT_TYPE_FIELD;
			break;
		default:
			return GF_BAD_PARAM;
		}
		gf_sg_script_field_new(dest, type, sf->fieldType, sf->name);
	}
	return GF_OK;
}

GF_Err gf_sg_script_field_get_info(GF_ScriptField *field, GF_FieldInfo *info)
{
	if (!field || !info) return GF_BAD_PARAM;
	memset(info, 0, sizeof(GF_FieldInfo));

	info->fieldIndex = field->ALL_index;
	info->eventType = field->eventType;
	info->fieldType = field->fieldType;
	info->name = field->name;

	//setup pointer (special cases for nodes)
	switch (field->fieldType) {
	case GF_SG_VRML_SFNODE:
	case GF_SG_VRML_MFNODE:
		info->far_ptr = &field->pField;
		info->NDTtype = NDT_SFWorldNode;
		break;
	default:
		info->far_ptr = field->pField;
		break;
	}
	return GF_OK;
}

void gf_sg_script_event_in(GF_Node *node, GF_FieldInfo *in_field)
{
	GF_ScriptPriv *priv = node->sgprivate->privateStack;
	if (priv->JS_EventIn) priv->JS_EventIn(node, in_field);
}

