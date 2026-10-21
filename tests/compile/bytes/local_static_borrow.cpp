import rstd;

void cannot_borrow_automatic_array() {
    const unsigned char data[] { 'a' };
    (void)rstd::bytes::Bytes::from_static<data>();
}
