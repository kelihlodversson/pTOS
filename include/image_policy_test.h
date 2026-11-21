/* Private regression transport; no production GEMDOS ABI assignment.
 * Disjoint from PRIVATE_SNAPSHOT_QUERY (0xc9710000), which is an exact value. */
#ifndef IMAGE_POLICY_TEST_H
#define IMAGE_POLICY_TEST_H
#define IMAGE_POLICY_CLONE 0xc9770000UL       /* | sizeof(clone args) */
#define IMAGE_POLICY_QUERY_DIGEST 0xc9771100UL
#define IMAGE_POLICY_QUERY_INSTALLED 0xc9771101UL
#define IMAGE_POLICY_FAIL_AFTER 0xc9771200UL  /* + stage 0..2 */
#endif
