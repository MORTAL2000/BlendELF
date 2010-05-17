
elf_entity* elf_create_entity(const char *name)
{
	elf_entity *entity;

	entity = (elf_entity*)malloc(sizeof(elf_entity));
	memset(entity, 0x0, sizeof(elf_entity));
	entity->type = ELF_ENTITY;

	elf_init_actor((elf_actor*)entity, ELF_FALSE);

	entity->scale.x = entity->scale.y = entity->scale.z = 1.0;
	entity->query = gfx_create_query();
	entity->visible = ELF_TRUE;

	entity->materials = elf_create_list();
	elf_inc_ref((elf_object*)entity->materials);

	entity->culled = ELF_TRUE;

	entity->dobject = elf_create_physics_object_box(0.2, 0.2, 0.2, 0.0, 0.0, 0.0, 0.0);
	elf_set_physics_object_actor(entity->dobject, (elf_actor*)entity);
	elf_inc_ref((elf_object*)entity->dobject);

	entity->armature_player = elf_create_frame_player();
	elf_inc_ref((elf_object*)entity->armature_player);

	if(name) entity->name = elf_create_string(name);

	global_obj_count++;

	return entity;
}

void elf_generate_entity_tangent_vectors(elf_entity *entity)
{
	elf_material *material;
	unsigned char found = ELF_FALSE;
	int i;

	if(!entity->model) return;

	for(material = (elf_material*)elf_begin_list(entity->materials); material;
		material = (elf_material*)elf_next_in_list(entity->materials))
	{
		for(i = 0; i < GFX_MAX_TEXTURES; i++)
		{
			if(material->textures[i] &&
				material->texture_types[i] == GFX_NORMAL_MAP &&
				!elf_get_model_tangents(entity->model))
			{
				elf_generate_model_tangent_vectors(entity->model);
				found = ELF_TRUE;
				break;
			}
		}
		if(found) break;
	}
}

elf_entity* elf_create_entity_from_pak(FILE *file, const char *name, elf_scene *scene)
{
	elf_entity *entity;
	elf_model *rmodel;
	elf_armature *rarmature;
	elf_material *rmaterial;
	unsigned int material_count;
	int i, j;
	int magic = 0;
	float scale[3] = {0.0, 0.0, 0.0};
	char model[64];
	char armature[64];
	char material[64];
	unsigned char shape;
	float mass, lin_damp, ang_damp;

	fread((char*)&magic, sizeof(int), 1, file);

	if(magic != 179532112)
	{
		elf_write_to_log("error: invalid entity \"%s//%s\", wrong magic number\n", elf_get_scene_file_path(scene), name);
		return NULL;
	}

	entity = elf_create_entity(NULL);
	elf_read_actor_header((elf_actor*)entity, file, scene);

	fread((char*)scale, sizeof(float), 3, file);

	fread(model, sizeof(char), 64, file);
	if(strlen(model))
	{
		rmodel = elf_get_or_load_model_by_name(scene, model);
		elf_set_entity_model(entity, rmodel);
	}

	fread(armature, sizeof(char), 64, file);
	if(strlen(armature))
	{
		rarmature = elf_get_or_load_armature_by_name(scene, armature);
		if(rarmature) elf_set_entity_armature(entity, rarmature);
	}

	// scale must be set after setting a model, setting a model resets the scale
	elf_set_entity_scale(entity, scale[0], scale[1], scale[2]);

	fread((char*)&shape, sizeof(unsigned char), 1, file);
	fread((char*)&mass, sizeof(float), 1, file);
	fread((char*)&lin_damp, sizeof(float), 1, file);
	fread((char*)&ang_damp, sizeof(float), 1, file);

	if(shape == ELF_BOX || shape == ELF_SPHERE || shape == ELF_MESH)
	{
		elf_set_entity_physics(entity, shape, mass);
		elf_set_actor_damping((elf_actor*)entity, lin_damp, ang_damp);
	}

	fread((char*)&material_count, sizeof(unsigned int), 1, file);
	for(i = 0, j = 0; i < (int)material_count; i++)
	{
		memset(material, 0x0, sizeof(char)*64);
		fread(material, sizeof(char), 64, file);

		rmaterial = NULL;
		if(strlen(material)) rmaterial = elf_get_or_load_material_by_name(scene, material);
		if(rmaterial)
		{
			if(elf_get_entity_material_count(entity) > j)
				elf_set_entity_material(entity, j, rmaterial);
			else elf_add_entity_material(entity, rmaterial);
			j++;
		}
	}

	// check if normal/displacement maps are needed
	elf_generate_entity_tangent_vectors(entity);

	return entity;
}

void elf_update_entity(elf_entity *entity)
{
	elf_update_actor((elf_actor*)entity);
	elf_update_frame_player(entity->armature_player);
}

void elf_entity_pre_draw(elf_entity *entity)
{
	gfx_get_transform_position(entity->transform, &entity->position.x);

	if(entity->armature && fabs(elf_get_frame_player_frame(entity->armature_player)-entity->prev_armature_frame) > 0.0001 &&
		elf_get_frame_player_frame(entity->armature_player) <= entity->armature->frame_count)
	{
		elf_deform_entity_with_armature(entity->armature, entity, elf_get_frame_player_frame(entity->armature_player));
		entity->prev_armature_frame = elf_get_frame_player_frame(entity->armature_player);
	}
}

void elf_destroy_entity(elf_entity *entity)
{
	elf_clean_actor((elf_actor*)entity);

	if(entity->model) elf_dec_ref((elf_object*)entity->model);
	if(entity->armature) elf_dec_ref((elf_object*)entity->armature);
	if(entity->vertices) gfx_dec_ref((gfx_object*)entity->vertices);
	if(entity->normals) gfx_dec_ref((gfx_object*)entity->normals);
	if(entity->query) gfx_destroy_query(entity->query);

	elf_dec_ref((elf_object*)entity->materials);
	elf_dec_ref((elf_object*)entity->armature_player);

	free(entity);

	global_obj_count--;
}

void elf_calc_entity_bounding_volumes(elf_entity *entity)
{
	float length;
	float max_scale;

	if(!entity->model)
	{
		entity->bb_min.x = entity->bb_min.y = entity->bb_min.z = -0.2;
		entity->bb_max.x = entity->bb_max.y = entity->bb_max.z = 0.2;
		entity->cull_radius = 0.0;
		return;
	}

	entity->bb_min = entity->model->bb_min;
	entity->bb_max = entity->model->bb_max;
	entity->bb_min.x *= entity->scale.x;
	entity->bb_min.y *= entity->scale.y;
	entity->bb_min.z *= entity->scale.z;
	entity->bb_max.x *= entity->scale.x;
	entity->bb_max.y *= entity->scale.y;
	entity->bb_max.z *= entity->scale.z;

	entity->cull_radius = entity->model->radius;

	if(entity->armature)
	{
		length = elf_get_vec3f_length(entity->armature->bb_min);
		if(length > entity->cull_radius) entity->cull_radius = length;
		length = elf_get_vec3f_length(entity->armature->bb_max);
		if(length > entity->cull_radius) entity->cull_radius = length;
	}

	max_scale = entity->scale.x;
	if(entity->scale.y > max_scale) max_scale = entity->scale.y;
	if(entity->scale.z > max_scale) max_scale = entity->scale.z;
	entity->cull_radius *= max_scale;
}

void elf_set_entity_scale(elf_entity *entity, float x, float y, float z)
{
	gfx_set_transform_scale(entity->transform, x, y, z);
	gfx_get_transform_scale(entity->transform, &entity->scale.x);

	if(entity->object) elf_set_physics_object_scale(entity->object, x, y, z);
	if(entity->dobject) elf_set_physics_object_scale(entity->dobject, x, y, z);

	elf_calc_entity_bounding_volumes(entity);
}

elf_vec3f elf_get_entity_scale(elf_entity *entity)
{
	elf_vec3f result;

	gfx_get_transform_scale(entity->transform, &result.x);

	return result;
}

void elf_set_entity_model(elf_entity *entity, elf_model *model)
{
	elf_material *material;

	if(entity->model) elf_dec_ref((elf_object*)entity->model);
	if(entity->vertices) gfx_dec_ref((gfx_object*)entity->vertices);
	if(entity->normals) gfx_dec_ref((gfx_object*)entity->normals);

	entity->model = model;
	entity->vertices = NULL;
	entity->normals = NULL;

	if(!entity->model)
	{
		if(entity->object) elf_disable_entity_physics(entity);
		elf_reset_entity_debug_physics_object(entity);
		elf_calc_entity_bounding_volumes(entity);
		return;
	}
	else
	{
		elf_inc_ref((elf_object*)entity->model);
	}

	elf_calc_entity_bounding_volumes(entity);

	while((int)entity->model->area_count > elf_get_entity_material_count(entity))
	{
		material = elf_create_material("");
		elf_add_entity_material(entity, material);
	}

	elf_set_entity_scale(entity, 1.0, 1.0, 1.0);

	if(entity->object) elf_set_entity_physics(entity, elf_get_physics_object_shape(entity->object), elf_get_physics_object_mass(entity->object));

	elf_reset_entity_debug_physics_object(entity);

	entity->moved = ELF_TRUE;
}

elf_model* elf_get_entity_model(elf_entity *entity)
{
	return entity->model;
}

int elf_get_entity_material_count(elf_entity *entity)
{
	return elf_get_list_length(entity->materials);
}

void elf_add_entity_material(elf_entity *entity, elf_material *material)
{
	elf_append_to_list(entity->materials, (elf_object*)material);
	// check if normal/displacmeent maps are needed
	elf_generate_entity_tangent_vectors(entity);
}

void elf_set_entity_material(elf_entity *entity, int idx, elf_material *material)
{
	elf_object *mat;
	int i;

	if(idx < 0 || idx > elf_get_list_length(entity->materials)-1) return;

	for(i = 0, mat = elf_begin_list(entity->materials); mat;
		mat = elf_next_in_list(entity->materials), i++)
	{
		if(idx == i)
		{
			elf_set_list_cur_ptr(entity->materials, (elf_object*)material);
			return;
		}
	}

	elf_generate_entity_tangent_vectors(entity);
}

elf_material* elf_get_entity_material(elf_entity *entity, int idx)
{
	if(idx < 0 || idx > elf_get_list_length(entity->materials)-1) return NULL;
	return (elf_material*)elf_get_item_from_list(entity->materials, idx);
}

void elf_set_entity_visible(elf_entity *entity, unsigned char visible)
{
	if(entity->visible == visible) return;

	entity->visible = (visible == ELF_FALSE) ? ELF_FALSE : ELF_TRUE;

	if(!entity->visible) entity->moved = ELF_TRUE;
}

unsigned char elf_get_entity_visible(elf_entity *entity)
{
	return entity->visible;
}

void elf_set_entity_physics(elf_entity *entity, int type, float mass)
{
	float position[3];
	float orient[4];
	float scale[3];
	float radius;
	elf_joint *joint;

	if(!entity->model) return;

	elf_disable_entity_physics(entity);

	switch(type)
	{
		case ELF_BOX:
			entity->object = elf_create_physics_object_box(
				(entity->model->bb_max.x-entity->model->bb_min.x)/2.0,
				(entity->model->bb_max.y-entity->model->bb_min.y)/2.0,
				(entity->model->bb_max.z-entity->model->bb_min.z)/2.0, mass,
				(entity->model->bb_max.x+entity->model->bb_min.x)/2.0,
				(entity->model->bb_max.y+entity->model->bb_min.y)/2.0,
				(entity->model->bb_max.z+entity->model->bb_min.z)/2.0);
			break;
		case ELF_SPHERE:
		{
			if(entity->model->bb_max.x-entity->model->bb_min.x >
				entity->model->bb_max.y-entity->model->bb_min.y &&
				entity->model->bb_max.x-entity->model->bb_min.x >
				entity->model->bb_max.z-entity->model->bb_min.z)
				radius = (entity->model->bb_max.x-entity->model->bb_min.x)/2.0;
			else if(entity->model->bb_max.y-entity->model->bb_min.y >
				entity->model->bb_max.x-entity->model->bb_min.x &&
				entity->model->bb_max.y-entity->model->bb_min.y >
				entity->model->bb_max.z-entity->model->bb_min.z)
				radius = (entity->model->bb_max.y-entity->model->bb_min.y)/2.0;
			else  radius = (entity->model->bb_max.z-entity->model->bb_min.z)/2.0;

			entity->object = elf_create_physics_object_sphere(radius, mass,
				(entity->model->bb_max.x+entity->model->bb_min.x)/2.0,
				(entity->model->bb_max.y+entity->model->bb_min.y)/2.0,
				(entity->model->bb_max.z+entity->model->bb_min.z)/2.0);
			break;
		}
		case ELF_MESH:
		{
			if(!elf_get_model_indices(entity->model)) return;
			if(!entity->model->tri_mesh)
			{
				entity->model->tri_mesh = elf_create_physics_tri_mesh(
					elf_get_model_vertices(entity->model),
					elf_get_model_indices(entity->model),
					elf_get_model_indice_count(entity->model));
				elf_inc_ref((elf_object*)entity->model->tri_mesh);
			}
			entity->object = elf_create_physics_object_mesh(entity->model->tri_mesh, mass);
			break;
		}
		default: return;
	}

	elf_set_physics_object_actor(entity->object, (elf_actor*)entity);
	elf_inc_ref((elf_object*)entity->object);

	gfx_get_transform_position(entity->transform, position);
	gfx_get_transform_orientation(entity->transform, orient);
	gfx_get_transform_scale(entity->transform, scale);

	elf_set_physics_object_position(entity->object, position[0], position[1], position[2]);
	elf_set_physics_object_orientation(entity->object, orient[0], orient[1], orient[2], orient[3]);
	elf_set_physics_object_scale(entity->object, scale[0], scale[1], scale[2]);

	if(entity->scene) elf_set_physics_object_world(entity->object, entity->scene->world);

	// things are seriously going to blow up if we don't update the joints
	// when a new physics object has been created
	for(joint = (elf_joint*)elf_begin_list(entity->joints); joint;
		joint = (elf_joint*)elf_next_in_list(entity->joints))
	{
		elf_set_joint_world(joint, NULL);
		if(entity->scene) elf_set_joint_world(joint, entity->scene->world);
	}
}

void elf_disable_entity_physics(elf_entity *entity)
{
	if(entity->object)
	{
		elf_set_physics_object_actor(entity->object, NULL);
		elf_set_physics_object_world(entity->object, NULL);
		elf_dec_ref((elf_object*)entity->object);
		entity->object = NULL;
	}
}

void elf_reset_entity_debug_physics_object(elf_entity *entity)
{
	float position[3];
	float orient[4];
	float scale[3];

	if(entity->dobject)
	{
		elf_set_physics_object_actor(entity->dobject, NULL);
		elf_set_physics_object_world(entity->dobject, NULL);
		elf_dec_ref((elf_object*)entity->dobject);
	}

	if(!entity->model)
	{
		entity->dobject = elf_create_physics_object_box(0.2, 0.2, 0.2, 0.0, 0.0, 0.0, 0.0);
	}
	else
	{
		entity->dobject = elf_create_physics_object_box(
			(entity->model->bb_max.x-entity->model->bb_min.x)/2.0,
			(entity->model->bb_max.y-entity->model->bb_min.y)/2.0,
			(entity->model->bb_max.z-entity->model->bb_min.z)/2.0, 0.0,
			(entity->model->bb_max.x+entity->model->bb_min.x)/2.0,
			(entity->model->bb_max.y+entity->model->bb_min.y)/2.0,
			(entity->model->bb_max.z+entity->model->bb_min.z)/2.0);
	}

	elf_set_physics_object_actor(entity->dobject, (elf_actor*)entity);
	elf_inc_ref((elf_object*)entity->dobject);

	gfx_get_transform_position(entity->transform, position);
	gfx_get_transform_orientation(entity->transform, orient);
	gfx_get_transform_scale(entity->transform, scale);

	elf_set_physics_object_position(entity->dobject, position[0], position[1], position[2]);
	elf_set_physics_object_orientation(entity->dobject, orient[0], orient[1], orient[2], orient[3]);
	elf_set_physics_object_scale(entity->dobject, scale[0], scale[1], scale[2]);

	if(entity->scene) elf_set_physics_object_world(entity->dobject, entity->scene->dworld);
}

void elf_set_entity_armature(elf_entity *entity, elf_armature *armature)
{
	if(entity->armature) elf_dec_ref((elf_object*)entity->armature);
	entity->armature = armature;
	if(entity->armature) elf_inc_ref((elf_object*)entity->armature);
	elf_calc_entity_bounding_volumes(entity);
}

void elf_set_entity_armature_frame(elf_entity *entity, float frame)
{
	elf_set_frame_player_frame(entity->armature_player, frame);
}

void elf_play_entity_armature(elf_entity *entity, float start, float end, float speed)
{
	elf_play_frame_player(entity->armature_player, start, end, speed);
	if(entity->armature) elf_deform_entity_with_armature(entity->armature, entity, start);
}

void elf_loop_entity_armature(elf_entity *entity, float start, float end, float speed)
{
	elf_loop_frame_player(entity->armature_player, start, end, speed);
	if(entity->armature) elf_deform_entity_with_armature(entity->armature, entity, start);
}

void elf_stop_entity_armature(elf_entity *entity)
{
	elf_stop_frame_player(entity->armature_player);
}

void elf_pause_entity_armature(elf_entity *entity)
{
	elf_stop_frame_player(entity->armature_player);
}

void elf_resume_entity_armature(elf_entity *entity)
{
	elf_stop_frame_player(entity->armature_player);
}

float elf_get_entity_armature_start(elf_entity *entity)
{
	return elf_get_frame_player_start(entity->armature_player);
}

float elf_get_entity_armature_end(elf_entity *entity)
{
	return elf_get_frame_player_end(entity->armature_player);
}

float elf_get_entity_armature_speed(elf_entity *entity)
{
	return elf_get_frame_player_speed(entity->armature_player);
}

float elf_get_entity_armature_frame(elf_entity *entity)
{
	return elf_get_frame_player_frame(entity->armature_player);
}

unsigned char elf_is_entity_armature_playing(elf_entity *entity)
{
	return elf_is_frame_player_playing(entity->armature_player);
}

unsigned char elf_is_entity_armature_paused(elf_entity *entity)
{
	return elf_is_frame_player_paused(entity->armature_player);
}

elf_armature* elf_get_entity_armature(elf_entity *entity)
{
	return entity->armature;
}

void elf_pre_draw_entity(elf_entity *entity)
{
	if(entity->armature)
	{
		if(entity->vertices) gfx_set_vertex_array_data(entity->model->vertex_array, GFX_VERTEX, entity->vertices);
		if(entity->normals) gfx_set_vertex_array_data(entity->model->vertex_array, GFX_NORMAL, entity->normals);
	}
}

void elf_post_draw_entity(elf_entity *entity)
{
	if(entity->armature)
	{
		if(entity->vertices) gfx_set_vertex_array_data(entity->model->vertex_array, GFX_VERTEX, entity->model->frames[0].vertices);
		if(entity->normals) gfx_set_vertex_array_data(entity->model->vertex_array, GFX_NORMAL, entity->model->normals);
	}
}

void elf_draw_entity(elf_entity *entity, gfx_shader_params *shader_params)
{
	if(!entity->model || !entity->visible) return;

	gfx_mul_matrix4_matrix4(gfx_get_transform_matrix(entity->transform),
			shader_params->camera_matrix, shader_params->modelview_matrix);

	elf_pre_draw_entity(entity);
	elf_draw_model(entity->materials, entity->model, shader_params, &entity->non_lit_flag);
	elf_post_draw_entity(entity);
}

void elf_draw_entity_ambient(elf_entity *entity, gfx_shader_params *shader_params)
{
	if(!entity->model || !entity->visible) return;

	gfx_mul_matrix4_matrix4(gfx_get_transform_matrix(entity->transform),
		shader_params->camera_matrix, shader_params->modelview_matrix);

	elf_pre_draw_entity(entity);
	elf_draw_model_ambient(entity->materials, entity->model, shader_params);
	elf_post_draw_entity(entity);
}

void elf_draw_entity_without_materials(elf_entity *entity, gfx_shader_params *shader_params)
{
	if(!entity->model || !entity->visible) return;

	gfx_mul_matrix4_matrix4(gfx_get_transform_matrix(entity->transform),
		shader_params->camera_matrix, shader_params->modelview_matrix);

	elf_pre_draw_entity(entity);
	elf_draw_model_without_materials(entity->materials, entity->model, shader_params);
	elf_post_draw_entity(entity);
}

void elf_draw_entity_bounding_box(elf_entity *entity, gfx_shader_params *shader_params)
{
	elf_vec3f bb_min;
	elf_vec3f bb_max;

	if(!entity->model || !entity->visible || !entity->model->vertex_array) return;

	gfx_mul_matrix4_matrix4(gfx_get_transform_matrix(entity->transform),
		shader_params->camera_matrix, shader_params->modelview_matrix);

	gfx_set_shader_params(shader_params);
	if(!entity->armature)
	{
		gfx_draw_bounding_box(&entity->model->bb_min.x, &entity->model->bb_max.x);
	}
	else
	{
		bb_min.x = bb_min.y = bb_min.z = -entity->cull_radius;
		bb_max.x = bb_max.y = bb_max.z = entity->cull_radius;
		gfx_draw_bounding_box(&bb_min.x, &bb_max.x);
	}
}

void elf_draw_entity_debug(elf_entity *entity, gfx_shader_params *shader_params)
{
	float min[3];
	float max[3];
	float *vertex_buffer;

	gfx_mul_matrix4_matrix4(gfx_get_transform_matrix(entity->transform),
		shader_params->camera_matrix, shader_params->modelview_matrix);

	memcpy(min, &entity->model->bb_min.x, sizeof(float)*3);
	memcpy(max, &entity->model->bb_max.x, sizeof(float)*3);

	gfx_set_color(&shader_params->material_params.color, 0.04, 0.04, 0.06, 1.0);
	gfx_set_shader_params(shader_params);
	gfx_draw_bounding_box(min, max);

	vertex_buffer = (float*)gfx_get_vertex_data_buffer(eng->lines);

	vertex_buffer[0] = min[0];
	vertex_buffer[1] = max[1];
	vertex_buffer[2] = max[2];
	vertex_buffer[3] = min[0];
	vertex_buffer[4] = max[1];
	vertex_buffer[5] = min[2];

	vertex_buffer[6] = min[0];
	vertex_buffer[7] = max[1];
	vertex_buffer[8] = min[2];
	vertex_buffer[9] = min[0];
	vertex_buffer[10] = min[1];
	vertex_buffer[11] = min[2];

	vertex_buffer[12] = min[0];
	vertex_buffer[13] = min[1];
	vertex_buffer[14] = min[2];
	vertex_buffer[15] = min[0];
	vertex_buffer[16] = min[1];
	vertex_buffer[17] = max[2];

	vertex_buffer[18] = min[0];
	vertex_buffer[19] = min[1];
	vertex_buffer[20] = max[2];
	vertex_buffer[21] = min[0];
	vertex_buffer[22] = max[1];
	vertex_buffer[23] = max[2];

	vertex_buffer[24] = max[0];
	vertex_buffer[25] = max[1];
	vertex_buffer[26] = max[2];
	vertex_buffer[27] = max[0];
	vertex_buffer[28] = max[1];
	vertex_buffer[29] = min[2];

	vertex_buffer[30] = max[0];
	vertex_buffer[31] = max[1];
	vertex_buffer[32] = min[2];
	vertex_buffer[33] = max[0];
	vertex_buffer[34] = min[1];
	vertex_buffer[35] = min[2];

	vertex_buffer[36] = max[0];
	vertex_buffer[37] = min[1];
	vertex_buffer[38] = min[2];
	vertex_buffer[39] = max[0];
	vertex_buffer[40] = min[1];
	vertex_buffer[41] = max[2];

	vertex_buffer[42] = max[0];
	vertex_buffer[43] = min[1];
	vertex_buffer[44] = max[2];
	vertex_buffer[45] = max[0];
	vertex_buffer[46] = max[1];
	vertex_buffer[47] = max[2];

	vertex_buffer[48] = min[0];
	vertex_buffer[49] = max[1];
	vertex_buffer[50] = max[2];
	vertex_buffer[51] = max[0];
	vertex_buffer[52] = max[1];
	vertex_buffer[53] = max[2];

	vertex_buffer[54] = min[0];
	vertex_buffer[55] = min[1];
	vertex_buffer[56] = max[2];
	vertex_buffer[57] = max[0];
	vertex_buffer[58] = min[1];
	vertex_buffer[59] = max[2];

	vertex_buffer[60] = min[0];
	vertex_buffer[61] = min[1];
	vertex_buffer[62] = min[2];
	vertex_buffer[63] = max[0];
	vertex_buffer[64] = min[1];
	vertex_buffer[65] = min[2];

	vertex_buffer[66] = min[0];
	vertex_buffer[67] = max[1];
	vertex_buffer[68] = min[2];
	vertex_buffer[69] = max[0];
	vertex_buffer[70] = max[1];
	vertex_buffer[71] = min[2];

	if(!entity->selected) gfx_set_color(&shader_params->material_params.color, 0.2f, 0.2f, 0.3f, 1.0f);
	else gfx_set_color(&shader_params->material_params.color, 1.0f, 0.0f, 0.0f, 1.0f);
	gfx_set_shader_params(shader_params);
	gfx_draw_lines(24, eng->lines);

	if(entity->armature)
	{
		elf_draw_armature_debug(entity->armature, shader_params);
	}
}

unsigned char elf_cull_entity(elf_entity *entity, elf_camera *camera)
{
	if(!entity->model || !entity->visible) return ELF_TRUE;

	return !elf_sphere_inside_frustum(camera, &entity->position.x, entity->cull_radius);
}

unsigned char elf_get_entity_changed(elf_entity *entity)
{
	return entity->moved;
}

