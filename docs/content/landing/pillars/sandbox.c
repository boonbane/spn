SPN_EXPORT spn_err_t generate_config(spn_t* spn) {
  const char* path = "/source/config.json";
  yyjson_doc* doc = yyjson_read_file(path, 0, NULL, NULL);
  yyjson_val* root = yyjson_doc_get_root(doc);
  yyjson_val* foo = yyjson_obj_get(root, "foo");

  FILE* header = fopen("/work/config.h", "w");
  fprintf(header, "#define FOO %d\n", yyjson_get_int(foo));
  fclose(header);
  return SPN_OK;
}
