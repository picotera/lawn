// based on djb2. see: http://www.cse.yorku.ca/~oz/hash.html
unsigned long string_hash(char *str)
{
    unsigned long hash = 5381;
    int c;

    while ((c = *str++) != '\0')
    {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}