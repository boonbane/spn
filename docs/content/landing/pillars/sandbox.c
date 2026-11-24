spn_err_t build(spn_t* spn) {
  read_json("endpoints.json");
  generate_code();
  add_to_build("/source/gen/endpoint.c");
}
